# Changelog — BZ_AutoDrive

> Versión EN: [CHANGELOG.en.md](CHANGELOG.en.md) · Licencia **MIT** · Proyecto **BrigadaZ**

Lo más nuevo arriba. Hasta la 1.1 la **conducción** (modelo *config-como-manual*) se mantuvo estable y los cambios fueron de robustez y herramientas. **La 1.2 cambia el cerebro de manejo**: además de leer el config, reproduce tu manejo y mide cómo se comporta cada vehículo.

---

## v1.2.4

**Arreglos que salieron de usar el framework en un mod real** (BZ_Trader, comerciantes que viajan en vehículos con paradas): el panel ya no se abre solo al cerrar el menú de un comerciante, y una parada pedida por otro mod sobre una marca con maniobra de reversa ya no desacomoda la ruta.

- **Corregido (admins): cerrar con ESC el menú de un comerciante abría el panel de AutoDrive.** Pasaba con el Market de Expansion (o cualquier menú de otro mod), la primera vez con cada comerciante. Ahora la acción que llega justo después de cerrar un menú ajeno no abre el panel; INICIO lo abre y lo cierra igual que siempre.
- **Corregido: después de una parada pedida por otro mod, el vehículo podía perder su grabación y terminar parando en otro lugar.** Si la toma tenía una maniobra de reversa justo después de la marca (marcar en avance y salir en reversa), al reanudar la grabación podía saltar cientos de metros adelante: el vehículo seguía solo los waypoints, sin la grabación, y la parada siguiente quedaba lejos de su marca. Ahora la grabación nunca se ubica a más de 24 m del vehículo (si algo no cierra, se re-ubica por la posición y el rumbo reales), un tramo de avance no consume el de reversa, y después de cada recuperación el cursor de la ruta y el de la grabación se re-sincronizan con la posición real.
- **Mejorado: la reversa cerca de una marca.** El conversor cuenta un cambio de sentido solo si el vehículo se desplaza al menos 3 m en el sentido nuevo: poner R un instante con el vehículo quieto ya no corta la ruta. **Las rutas ya convertidas no hace falta reconvertirlas**: al cargarlas, un tramo de reversa de menos de 3 m entre dos de avance se funde solo. **Consejo para grabar:** las marcas (NUMPAD 4) se hacen siempre en avance; si venís en reversa, poné primera, frenado, y marcá.
- **Mejorado: el registro.** Cada línea de la grabación (`[Cinta]`) dice de qué vehículo es, y una línea `DESFASE` avisa cada 5 s si el cursor de la ruta y el vehículo no coinciden.

---

## v1.2.3

**Arreglos importantes para cualquier server, y motos mucho más precisas.** Dos errores que traían la 1.2.1 y la 1.2.2, y una tanda de mejoras en las motos medida en Badlands y en Chernarus.

- **Corregido: los vehículos de AutoDrive quedaban guardados en el mundo al cerrar el server.** Si el server se cerraba bien (por ejemplo, con la X de la ventana) mientras una ruta andaba, el vehículo quedaba guardado en la persistencia; al arrancar de nuevo seguía ahí, sin chofer, sobre la ruta, y la vuelta siguiente chocaba contra él. Ahora nada de lo que crea AutoDrive se guarda: vehículos, choferes, guardias, tripulación y carteles de parada. **Los vehículos que ya quedaron guardados con versiones anteriores no se pueden distinguir de los demás: borralos una vez a mano** (por ejemplo, con COT).
- **Corregido (admins): ESC podía quedar como la tecla del panel.** Después de cerrar con ESC el menú de otro mod (por ejemplo, el Market de Expansion), cada ESC abría o cerraba el panel de AutoDrive y el menú de pausa no volvía a aparecer: había que cerrar el juego desde el Administrador de tareas. Ahora las teclas de los menús (ESC, ENTER, BACKSPACE, TAB, ESPACIO, Windows) nunca pueden quedar como tecla del panel.
- **Motos: paradas pedidas por otro mod** (por ejemplo, un comerciante que se detiene a vender con `HoldRunner`): la moto se detiene justo donde se detuvo la grabación (una sola parada, antes paraba 4 m antes y otra vez en la marca) y al seguir arranca en el acto **en 1ra** (antes salía en la marcha grabada y tardaba segundos).
- **Motos: a paso de hombre (0-3 km/h) ya no corrige hacia los costados.** A esa velocidad el manubrio responde distinto; antes, al arrancar de una parada un poco corrida de la línea, la moto podía girar sobre sí misma y terminar re-ubicada.
- **Motos: la dirección sigue la forma del camino grabado** en lugar de repetir los toques de teclado de la grabación. Con teclado el volante va a fondo o a cero, y la moto, que responde más que el jugador, ganaba un poco de giro en cada toque: en curvas cerradas a 60-75 km/h se metía 1,5-2,5 m por adentro. Medido en una ruta de Badlands: de 2,2-2,4 m a menos de 1 m en una curva y de 2,1 a ~1,3 m en otra (con un pozo del terreno que también está en la grabación); en Chernarus las tres rutas de moto también mejoran (Enduro 0,37 → 0,30 m, Moped 0,86 → 0,46 m).
- **Motos: la moto mide cómo responde al manubrio** por franjas de 10 km/h mientras anda, y guarda esa medida por ruta y vehículo (`BZ12_AUT_<ruta>_<clase>.json` en el perfil de AutoDrive) para la vuelta siguiente y para el próximo arranque del server. La primera vuelta de una ruta nueva arranca con lo que mide de la grabación.
- **Motos: la ley por inclinación actúa de 75 a 85 km/h** (antes de 65 a 85): por debajo de 75 la moto dobla casi sin inclinarse al equilibrio y esa ley la empujaba hacia adentro.
- **Mejorado: el registro dice contra qué choca.** "Rueda en el aire" y toda frenada brusca sin freno propio (más de 10 km/h perdidos en 0,3 s) dicen el último contacto (clase o modelo del objeto) y la posición.

---

## v1.2.2

**Arreglo urgente: compatibilidad con otros mods.** La 1.2.1 no compilaba en un server que tuviera ciertos mods: el server no arrancaba (`Can't compile "World" script module`).

- **Corregido: el server no arrancaba con el CarPack actualizado para la 1.30** (y con otros packs que definen clases con nombres cortos, como `door` o `plate`). Esos mods traen vehículos u objetos cuyo nombre interno es una palabra corta (`esc`), y en DayZ cada clase de configuración es también un nombre reservado para los scripts. Tres variables de AutoDrive se llamaban igual y el compilador no lo acepta: `Variable name 'esc' already used as type name`. Ahora tienen nombres propios. **No cambia nada de la conducción ni del comportamiento.**
- **Mejorado: cómo lo evitamos de acá en más.** Antes de cada versión, los nombres de las variables del framework se revisan contra los nombres de clase de más de 1.400 PBO del Workshop (226 mods), y la prueba es parte de la validación.

---

## v1.2.1

**DayZ 1.30, las motos, y un framework mucho más exacto.** La 1.2 publicada no compila en la 1.30: la 1.2.1 corre en las dos versiones con un solo PBO. Además trae las motos (experimental) y una tanda grande de precisión: cada ruta se da por completa solo si termina bien de verdad, y el vehículo sigue tu línea también en los cambios de sentido y en las paradas.

### DayZ 1.30

- **Probado el día del lanzamiento (15/10)** en la 1.30 estable, sin Expansion y con Expansion 1.9.79: chofer eAI en autos y en motos, y el retiro con un jugador sentado de pasajero (el vehículo espera a que se baje).
- **Nuevo: un solo PBO para la 1.29 y la 1.30.** El mismo archivo compila y corre en las dos versiones.
- **Cambiado: las luces usan el sistema nuevo de la 1.30.** En la 1.29 siguen como antes.
- **Corregido: retirar un vehículo con alguien sentado.** En la 1.30, borrar un vehículo ocupado tira el server. El framework espera a que quede vacío antes de retirarlo. Si el que está sentado es un jugador, el vehículo queda frenado y no se borra hasta que se baje.
- **Corregido: la lista de rutas en la 1.30.** Con la 1.2 salía vacía. Necesita que el server arranque con un `-profiles` **relativo** (p. ej. `-profiles=profiles`): con una ruta absoluta la 1.30 no deja listar archivos, y el RPT lo avisa al arrancar (`DayZ 1.30 cannot list files under an absolute -profiles path…`).
- **Nuevo: las motos (experimental).** El Moped y la Enduro de la 1.30 no son autos (no usan CarScript) y tienen un conductor propio: freno delantero y trasero por separado (sin reversa en la moto), una ley de inclinación y amortiguación medida para cada moto, y llegadas de 1-3 cm. **Caminar con la moto** (bajarse y empujarla, hacia adelante o hacia atrás) se graba y se reproduce con la animación. Si una moto se traba, retrocede caminando 3 m y reintenta. Ver el manual, §9B.
- **Mejorado: la moto a alta velocidad sigue tu línea.** Una moto tarda medio segundo en inclinarse cuando gira el manubrio; corregirla como a un auto la hacía oscilar y abrirse en las curvas rápidas (hasta 3,7 m). Arriba de 65 km/h ahora corrige inclinándola según la curva del camino que viene. Medido con la Enduro hasta 120 km/h: desvío máximo 0,4-0,56 m en toda la ruta.
- **Mejorado: el final de la moto.** Frena al punto midiendo, en esa misma frenada, cuánto frena de verdad la moto. Llega a 3-4 cm también entrando rápido (antes se podía pasar hasta 0,7 m).
- **Mejorado: caminar con la moto.** Si en la toma la doblaste mientras la empujabas (sacarla marcha atrás y encararla para otro lado), el NPC reproduce ese giro y corrige si se corre de la línea. Al terminar de caminar arranca a fondo, después de respetar la espera que hayas hecho.
- **Corregido: el velocímetro que no da un número.** En la 1.30, un vehículo recién aparecido puede tener el velocímetro sin un valor válido durante unos segundos (lo vimos en el M1025, de caja automática), y la ruta quedaba trabada en el arranque. Ahora el framework usa la velocidad real del vehículo mientras dure y lo avisa en el RPT: `[Velocimetro] GetSpeedometerAbsolute no da un numero … el framework usa la velocidad del cuerpo`.

### Cuándo una ruta está completa (el veredicto)

- **Cambiado: `ROUTE_COMPLETE` es estricto.** Una ruta cuenta como completa solo si se cumplen las tres: el vehículo quedó **quieto a 0,25 m o menos** del punto final de la toma, **no chocó contra ningún objeto** y **el AutoRecovery no tuvo que moverlo** (maniobra o reubicación). Los re-arranques suaves del AutoRecovery no la invalidan, pero se listan. La red de 1 m de la 1.2 ya no aprueba.
- **Cambiado: el fin de ruta se decide con el vehículo QUIETO.** El `FIN` y los eventos de fin salen cuando el vehículo se detuvo (a lo sumo unos 3 s después de llegar), con la línea `REPOSO tras T s: a X m de la pose final (longitudinal …, lateral …)`. Lo mismo para las motos.
- **Nuevo: el motivo de un `ROUTE_INCOMPLETE`**, en la misma línea: a cuánto quedó, contra qué chocó y en qué punto, o qué intervención física hizo el AutoRecovery.

### Conducción: más exacta

- **Mejorado: la frenada final.** El freno del config del vehículo resultó optimista (8,5 m/s² declarados contra 5,5 reales en un Sedan). Ahora el final de la ruta se planea con **la frenada que mediste vos en la toma** (`FRENADA de la toma` en el log), y cada cambio de sentido con la de ese punto (`FRENADA del cusp`). Llegadas: 0,10-0,15 m en todas las rutas de prueba. La frenada de la toma ahora se mide también en paradas cortas (desde 8 km/h).
- **Mejorado: los cambios de sentido.** El vehículo sigue tu línea también a baja velocidad y en reversa: al cambiar de marcha queda a centímetros del punto (antes, 0,5-1 m pasado o corrido) y el tramo de reversa arranca en su lugar. En los últimos metros antes de cada cambio de marcha corrige el desvío lateral con más fuerza.
- **Corregido: las paradas grabadas en medio de un tramo.** Se cumplen por el tiempo que grabaste y el vehículo sigue; nunca lo retienen. Y el tramo se da por terminado recién en tu **última** parada antes de cambiar de marcha, no en una anterior.
- **Nuevo: el freno al final de cada tramo.** Si la toma se termina (o sigue a paso de hombre sin freno) con el vehículo todavía rodando, el framework frena hasta el punto del cambio de marcha.
- **Corregido: gas y freno a la vez.** Cuando la toma frena, el framework ya no suma gas para alcanzar una velocidad grabada.
- **Mejorado: tomas con correcciones de posición.** Si mientras grababas el server te corrigió la posición hacia atrás (un "tirón"), el vehículo ya no sigue ese retroceso.

### Grabación y Wizard

- **Corregido: el reloj del grabador.** Las tomas nuevas guardan el tiempo real. Una toma con el reloj roto (grabada con versiones anteriores) no se convierte y el Wizard lo dice.
- **Nuevo: avisos de la toma** al convertir, en el idioma del Wizard: `(!) La toma tiene N retroceso(s) grabado(s)` (el server te corrigió la posición al grabar: si el NPC se desvía ahí, regrabá ese tramo) y `(!) En N cambio(s) de marcha el vehiculo avanzo sin freno antes de cambiar` (al grabar, mantené el freno pisado hasta poner la otra marcha). Los choques de la toma también se avisan.
- **Cambiado: el punto de un cambio de sentido** se pone donde el vehículo quedó quieto en la toma, no donde bajó de 1 km/h. Vale para las rutas que convertís con esta versión.
- **Cambiado: las tomas nuevas graban más datos por cuadro** (inclinación, contactos, reloj real). Por eso una parte grabada con la 1.2 no se une con una de la 1.2.1 en **[3] Merge de tramos**: grabá todas las partes con la misma versión.

### El panel

- **Corregido: en la 1.30, el panel se cierra con INICIO** (o la tecla que le asignes en Controles), igual que se abre. **ESC** también lo cierra, sin abrir la pausa del juego.
- **Corregido: después de cerrarlo con ESC, INICIO lo vuelve a abrir al primer intento.** En la 1.30 había que apretar la tecla dos veces.
- **Nuevo: SPAWN EMPTY VEHICLE revisa que el lugar esté libre.** Si ya hay un vehículo en ese punto, no crea nada y lo avisa en pantalla: `Spawn point occupied by <clase> (N m away)`.
- **Mejorado: cuando un spawn vacío no se puede hacer** (lugar ocupado, una ruta que no carga o no tiene waypoints, una clase que no existe), el aviso sale en pantalla y la fila `LOADING` desaparece en el acto. Antes quedaba 15 s esperando.
- **Corregido: la rueda del mouse sobre la lista de rutas.** Cada muesca le volvía a pedir al server la ficha de la ruta, y el server releía la ruta del disco. Ahora la pide solo el clic, y el scrubber ya no vuelve al principio cuando girás la rueda.

### Logs y diagnóstico

- **Nuevo: `Diagnostics`** en el settings (apagado por defecto): con el diagnóstico apagado el RPT de un server con rutas en marcha baja más del 90 %. Quedan las líneas de carga, fin, errores y AutoRecovery.
- **Nuevo: cajas negras opcionales** (`BlackBox`, apagado por defecto), con rotación: `BlackBoxKeepPerRoute` guarda las últimas N por ruta (10 por defecto).
- **Mejorado: la línea de un choque** (`*** CHOQUE ***`) dice contra qué fue (la clase del objeto, o el terreno), en qué zona del vehículo y qué ruedas tocaban el piso justo antes.
- **Mejorado: cuando el framework detiene un vehículo por seguridad** (`SAFE_STOP`), el log dice por qué.
- **Nuevo: el aviso de las rutas de formato viejo** (sin `_wp.csv`). Una ruta así se carga entera de una vez y congela el server mientras la lee. Ahora el RPT lo dice, y también a los admins conectados (según `AdminSystemNotifications`): `route <archivo> is in the old format (no _wp.csv): loading it froze the server for N s…`. Volvé a convertir la toma con el Wizard y la ruta carga de a partes.
- **Corregido: `[LOADTIME]` da los milisegundos reales.** Hasta la 1.2 el número salía 10.000 veces más grande.
- **Nuevo: el diagnóstico del panel.** Con `Diagnostics` en 1, el log del cliente de un admin registra cuándo se abre y se cierra el panel y por qué (`[Reproductor] ABIERTO` / `CERRADO (tecla|ESC|boton|otro)`). Sirve si la tecla no responde.

### Para mods que usan AutoDrive (la API del runner)

- **Cambiado:** el fin de ruta (`PollEvent`, `RunnerRouteEnded/Completed/EndReason`) llega en el reposo, hasta ~3,5 s después de la llegada, y `RunnerRouteCompleted()` usa el veredicto estricto.
- **Nuevo:** `RunnerReposoFinM()`, la distancia en reposo con la que se decidió (autos y motos).
- **Cambiado: campos nuevos en el log y en las cajas negras.** La línea `LEG n cerrado` suma el lateral (`lat`, entre `remFr` y `rem`) y la distancia a la última parada grabada de la fase; las cajas negras del auto suman columnas (contactos y `frenoFin`). Si una herramienta tuya las lee por posición, revisala.
- **Cambiado: la caja negra de la moto suma cuatro columnas al final** (`roll_des`, `mezcla`, `w_cmd`, `vol_cam`); las anteriores no se mueven.

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
- **Corregido: la instalación desde cero.** El framework crea sus carpetas del perfil. Antes, en un server nuevo, el registro de un jugador manejando intentaba escribir en una carpeta que no existía, en cada cuadro.
- **Nuevo: avisos de arranque para una instalación nueva.** `AdminSteamIDs is empty: nobody is admin…` si todavía no hay admins, y `No routes yet…` si no hay ninguna ruta. Ya no se genera una ruta de plantilla.

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
