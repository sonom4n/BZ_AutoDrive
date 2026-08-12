# BZ_AutoDrive — Manual del administrador

> Framework de conducción de vehículos por NPC para DayZ. Versión didáctica para admins/modders.
> **Open source (MIT):** el código, este manual y el AI knowledge pack son públicos — descargalo, usalo en tu server, forkealo, extendelo y compartí tus rutas.
> *Manual v1.0 — guía práctica del administrador. Reemplaza al MANUAL_eAI_VEHICLES como guía operativa.*

---

## Contenido

1. ¿Qué es BZ_AutoDrive?
2. La idea, en una frase
3. El modelo de uso: bajalo, adaptalo, es tu mod
4. Requisitos
5. Inicio rápido — tu primera ruta
6. El wizard en detalle
6B. El editor de trayectorias y mapas — *dibujá en vez de grabar*
7. Cómo grabar bien
8. El servicio de bus permanente
9. Cómo maneja el NPC (sigue tu línea y tu velocidad)
10. Eventos y secuencias
11. El grafo — red de rutas
12. Misiones — integración con Quests
13. Controles (teclas)
14. La interfaz (UI)
15. Audio
16. Si algo no anda
17. Ideas
18. Alcance y límites
19. Frontera abierta
- Apéndice A — Referencia completa de la config de ruta
- Apéndice B — Para desarrolladores (extender con código)

---

## 1. ¿Qué es BZ_AutoDrive?

BZ_AutoDrive hace que un **NPC maneje un vehículo de DayZ de forma autónoma**, siguiendo una ruta que **vos grabaste manejando**. No es teleport ni movimiento "falso": el NPC realmente **acelera, frena y gira**, usando la física real del juego — el auto se comporta como cuando lo manejás vos.

**¿Para qué te sirve?** Para cualquier cosa que necesite un vehículo manejado por IA:

- Un **bus de línea** que recorre la costa parando en cada pueblo.
- Un **taxi** que lleva jugadores de un punto a otro.
- Un **convoy de refuerzos** que llega a una zona de misión y despliega bots.
- Una **patrulla** motorizada que da vueltas por una zona.

> *Ejemplo:* querés un colectivo que una tres pueblos. Lo manejás vos **una sola vez** (grabás el recorrido), y a partir de ahí un NPC lo repite solo, las veces que quieras, sin que vos estés.

---

## 2. La idea, en una frase

> **Vos manejás la ruta una vez; el framework aprende tu línea y lee cómo es el vehículo; después un NPC la maneja.**

Lo potente está en el "lee cómo es el vehículo": BZ_AutoDrive **lee el config del vehículo** (su dirección, sus marchas, su motor) y maneja según eso. Por eso **el framework maneja cualquier vehículo sin setup por-auto** — no calibrás nada por-vehículo, la física declarada alcanza.

> *Ojo con la grabación:* una grabación captura la corrida de **su** vehículo (su *fingerprint*, su freno medido, su marcha) — **es de ese auto**. ¿Querés la **misma traza** en otro vehículo? Eso se hace en el **editor**: cargás/dibujás la traza y le **asignás el vehículo** que quieras (§6B). La **traza dibujada es lo universal**; la grabación es de su auto. Cada vehículo la corre según lo que su física le permite (el chico clava la línea, el bus la hace más lento pero llega).

*(Por qué importa: DayZ no tiene "manejá hasta acá" para autos IA — solo te da el volante y los pedales. BZ_AutoDrive es el piloto que faltaba.)*

---

## 3. El modelo de uso: bajalo, adaptalo, es tu mod

BZ_AutoDrive es un **punto de partida**, no un producto cerrado. El arco para cualquier admin es siempre el mismo:

1. **Descargás** el framework (open source).
2. **Lo adaptás a tu server:** grabás *tus* rutas y configurás *tus* escenarios. El **motor de manejo ya viene integrado** — no programás cómo maneja el NPC, eso ya está resuelto.
3. **Te queda tu mod**, con el framework adentro + tu contenido, corriendo lo tuyo.

Lo que agregás depende de para qué lo quieras:
- **Servicio de bus / taxi** → grabás la(s) ruta(s) y las dejás corriendo (§8). Sin tocar código.
- **Sistema de misiones** → sumás los Quests + las rutas de convoy/escolta (§12). El framework maneja los vehículos de tus misiones.
- **Algo a medida** → lo extendés con código (Apéndice B) o construís tu propio mod sobre él como dependencia (B.6).

> La gracia del open source: **todos bajan el mismo motor y lo hacen suyo.** Uno arma un bus de costa, otro un taxi urbano, otro una campaña de misiones — sin que ninguno tenga que reinventar el manejo.

> *Si lo repackageás como tu propio addon* (renombrarlo, tu Workshop): rebuildeás con tu propia key y copiás el `.bikey` a `keys/`. La licencia MIT lo permite — solo mantené los créditos.

---

## 4. Requisitos

- **DayZ server** con el mod **BZ_AutoDrive** cargado + su `.bikey` en `keys/`.
- **DayZ-Expansion-AI (eAI)** — *imprescindible*: el NPC chofer sale de acá. El framework **conduce**; eAI provee el "cuerpo" del NPC.
- **DayZ-Expansion-Quests** — *solo* si vas a hacer **misiones con bots** (§12). Para transporte / taxi / patrulla **no** hace falta.
- **El wizard** (`route_wizard.ps1`) corre en **tu PC** (fuera del juego), no en el server. Lo arrancás con doble clic en `tools\Wizard.bat` — no necesitás abrir PowerShell ni tipear nada.
- **Python 3 (en tu PC)** — *imprescindible para convertir tomas*: el conversor del wizard (`frame_to_route.py`) y el import de v1 son Python. Sin Python, el wizard abre pero **no puede convertir** una grabación en ruta. Se instala **una vez** desde [python.org/downloads](https://www.python.org/downloads/) — en el instalador marcá **"Add Python to PATH"**. *(El mod, el editor de trayectorias y las rutas ya convertidas **no** lo necesitan — Python solo hace falta para el paso de **convertir/importar** tomas.)*

---

## 5. Inicio rápido — tu primera ruta

Vamos a armar una ruta de punta a punta. *Ejemplo que seguimos: un bus que va de la terminal de un pueblo a otro.*

### Paso 0 — Revisá tus teclas *(hacelo ANTES de nada)*

El framework usa **solo 3 teclas**, ya asignadas por default. Entrá a **Opciones → Controles → categoría "BZ AutoDrive"** y verificalas:

| Acción (en el menú) | Default |
|---|---|
| **Open Control Panel** | `INICIO` (Home) |
| **Record (start/stop)** | `NUMPAD 5` |
| **Mark Event / Stop** | `NUMPAD 4` |

> **⚠ Lo primero: fijate que no choquen con tus teclas.** Si ya tenés `INICIO` o `NUMPAD 4/5` asignadas a **otro mod** (o a una acción de DayZ), se van a **pisar** y algo no va a responder — y **DayZ no te avisa del conflicto**. Por eso, antes de grabar: abrí esa categoría, mirá que las 3 no colisionen con lo tuyo, y **reasigná** la que haga falta (clic en la tecla → apretás la nueva). Es un minuto y te ahorra el clásico *"grabo y no pasa nada"* o *"aprieto INICIO y no abre el panel"*.

*(Estas teclas solo tienen efecto para el **admin** —§14.1— aunque todos los jugadores las VEN en su menú de Controles. Detalle completo de los controles en §13.)*

### Paso 1 — Grabá la ruta (la manejás vos)
Subite al vehículo y manejá el recorrido **como querés que lo haga el NPC**.
- **NUMPAD 5** → empezá a grabar. Manejá tranquilo, a la velocidad que querés que vaya.
- **NUMPAD 4** → tocalo en cada **parada o punto importante** (la terminal, una esquina donde pasa algo). Marca ese punto para después poder colgarle eventos.
- **NUMPAD 5** de nuevo → terminá de grabar.

> Queda un archivo con tu recorrido: tu **línea** + tu **velocidad** + **dónde parás**. Eso es la materia prima.

> **🔑 Lo más importante que pasa al grabar (y que no ves):** ese mismo NUMPAD 5, además de tu recorrido, **le saca una foto completa al vehículo** — su *fingerprint*. Queda en un `header_*.txt` al lado de tu grabación, y trae **todos los datos del auto, solos**:
> - el **classname**, el **wheelbase** (distancia entre ejes) y el **ángulo de dirección** → de ahí se calcula el **R_min** (el radio más chico que puede girar),
> - la cantidad de **marchas**, la **masa**, las **RPM** del motor,
> - y la lista **real de sus partes** (ruedas, puertas, batería, radiador, bujía…).
>
> Por eso **no configurás nada del vehículo a mano**: el wizard y el converter leen ese fingerprint y arman la ruta a su medida — calculan cuánto puede doblar, le ponen los attachments correctos (*no se adivinan, son los que el vehículo tenía*) y lo manejan según *su* física. Es el corazón del "config como manual de manejo".
>
> *Ejemplo real:* grabamos un UAZ-452 y de **un solo NUMPAD 5** salió todo: `UAZ_452`, wheelbase 2.53 m, R_min 3.9 m, 6 marchas, masa 2860 kg, + sus 4 ruedas (con la de auxilio), 6 puertas y batería/radiador/bujía. Cero configuración, cero adivinar.

### Paso 2 — Convertí la grabación (el wizard)
El wizard es un **conversor**: agarra tu grabación cruda —atada al auto con el que manejaste— y la **convierte** en una ruta lista, **"del recorrido" y no "de los pedales de tu auto"** (la velocidad pasa a salir del modelo inverso que lee el config, no de tus pedales crudos), y **desplegada** en el Reproductor. *No calibra nada a mano — la fidelidad sale de leer el config del vehículo en vivo (el fingerprint), no de un ajuste por-ruta.*

Arrancá el wizard con **doble clic en `tools\Wizard.bat`** (no hace falta abrir PowerShell ni tipear nada), elegís **[1] Convertir**, elegís tu grabación y le ponés un **nombre** — y te deja la ruta lista.

> **La primera vez** que abrís el wizard te pide tus **paths** — la carpeta de rutas de tu server (`RoutesDir`) y dónde están tus grabaciones. Los seteás **una sola vez**, el wizard se los acuerda, y después los cambiás cuando quieras con **[6] Configurar paths** (detalle en §6).

> *¿Por qué este paso?* Sin él, el NPC manejaría con los pedales exactos de **tu** auto — y en otro vehículo se rompería. El wizard hace que la ruta sea **"del recorrido", no "del auto"**.
> *¿Dónde está mi grabación?* Vive en el **cliente**, la PC donde manejaste: `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`. El wizard la busca ahí sola. La ruta convertida se **deploya** a tu carpeta de rutas del server (`RoutesDir`). Todo el detalle —el "trío" de archivos y las carpetas— en **§6**.

### Paso 3 — Deploy (ya lo hace el wizard)
Al convertir, el wizard corre los chequeos + el split y deja la ruta **desplegada y lista en el Reproductor** (carga en caliente, **sin reiniciar el server**). No editás ningún archivo a mano. Si tenés un segundo server, el wizard te ofrece copiarla al server B (los paths los configurás en **[6] Configurar paths**).

### Paso 4 — Probalo (que el NPC la maneje)
Abrí el **Reproductor** (la UI del admin) → elegí tu ruta de la lista → el NPC aparece y arranca a manejar. **Sin reiniciar el server.**

> *Ejemplo:* elegís "Bus pueblo" en la lista y ahí nomás el colectivo sale de la terminal, manejado por el NPC, parando donde marcaste.

**Eso es todo el ciclo:** `grabás → wizard → deploy → play`. El resto del manual es para sacarle más jugo a cada parte.

---

## 6. El wizard en detalle — tu herramienta central

El **wizard** (`tools\route_wizard.ps1`) convierte tu grabación cruda en una ruta lista, desplegada en el Reproductor. Es un menú interactivo (TUI) — corre en tu PC, fuera del juego. *(Es un **conversor**: no calibra ni te hace un cuestionario — el manejo sale de leer el config del vehículo.)*

> **Arrancalo con doble clic en `tools\Wizard.bat`** — abre el wizard directo, sin abrir PowerShell ni tipear comandos. (El `.bat` usa `-ExecutionPolicy Bypass`, que **solo afecta esa ejecución** — no cambia la política de tu sistema.)

> **Principio: todo pasa por el wizard.** El wizard corre las sub-herramientas (`csv_to_route`, `route_split`) **por vos**, en orden. Vos **no** editás el JSON a mano ni corrés los `.ps1` sueltos — el wizard es la única puerta de entrada.

### ⚠ Dónde se aloja cada toma (leé esto)

Cuando grabás (NUMPAD 5), el archivo **no queda donde lo buscarías intuitivamente**. La regla de oro:

- La **grabación humana** (`path_*.csv` / `frame_*.csv`, la que hacés con NUMPAD 5) cae en el **cliente** (la PC donde manejaste): `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`.
- Las **corridas de Boris** (`ai_run_*.csv` y `boris_native_*.csv`) se arman con los **checks del Reproductor** (§14, ya **no con teclas**) y caen en el **server que corrió**, no en el cliente. *Son mediciones para diagnosticar/comparar, no tomas para convertir (§16.1–16.2).*

El wizard busca tus grabaciones en el **cliente** automáticamente, así que normalmente la encuentra sola. Si corrés el server en otra PC, también podés apuntarle a esa carpeta:

| Lugar | Carpeta |
|---|---|
| Cliente (donde manejás) | `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\` |
| Server (si grabaste ahí) | `<server>\profiles\BZ_AutoDrive_PathLogger\` |

> Si grabaste y "no aparece", fijate en qué máquina estabas — cae en el cliente de esa PC.

Dos archivos más que conviene conocer:

- Junto a cada grabación hay un **`header_*.txt`** = el **fingerprint del vehículo** (dirección, marchas, wheelbase). El wizard lo **lee** para armar la ruta a la medida de ese auto (no lo calibra: lo lee).
- Las **rutas terminadas** (el "trío") viven en tu carpeta de rutas del server (`RoutesDir` — ver `[6]`).

### El menú del wizard
Arrancás el wizard (doble clic en `Wizard.bat`) y te aparece el menú:

> **[1] Convertir · [2] Importar ruta de BrigadaZ Transport v1 · [6] Configurar paths · [Q] Salir**

#### [1] Convertir — de grabación a ruta lista
Agarrás una grabación cruda (`frame_*.csv` + su `header_*.txt` apareado) y el wizard la vuelve una **ruta desplegada y lista en el Reproductor**, en un paso. Lo único que te pide es un **NOMBRE** → la ruta sale como `BZBusRoute_<nombre>` y aparece con ese nombre en el Reproductor.

**Qué genera y dónde va.** De una grabación salen **tres archivos** (el "trío" que lee el server), todos en tu **carpeta de rutas** (`RoutesDir` — ver `[6]`):

| Archivo | Qué es |
|---|---|
| `BZBusRoute_<nombre>.json` | el **master** editable (header + todos los waypoints) |
| `BZBusRoute_<nombre>_hdr.json` | el **header** solo — vehículo, fingerprint, config de manejo (sin waypoints) |
| `BZBusRoute_<nombre>_wp.csv` | los **waypoints** en 21 columnas (*fast-load*: el server los lee sin parsear el JSON) |

> Corre los **chequeos + el split** solo y te avisa que quedó **desplegada** (carga en caliente, **sin reiniciar**). Si configuraste un 2º server, te ofrece **copiar el trío** allá. *Tip:* convertí el mismo CSV **dos veces** con nombre+modo distinto (`costa_m1` / `costa_m3`) → las dos quedan en el Reproductor para comparar.

#### [2] Importar ruta de BrigadaZ Transport v1
¿Venías usando **BrigadaZ Transport v1** y ya tenés un recorrido grabado? **No lo grabes de nuevo** — esta opción lo trae al formato de BZ_AutoDrive.
- **Dónde busca el archivo.** La ruta de v1 **no está en el PBO**: vive en el profile de **tu** server, y esa carpeta **puede llamarse de cualquier forma** (un repack/fork le cambia el nombre), así que el wizard **no la adivina: se la indicás vos** con **[B]** (y se la acuerda). Apuntale a tu `profiles\` y busca en **todas las subcarpetas**. También mira la carpeta **`_importar`** que te crea dentro de tu carpeta de rutas (buzón para el JSON que te traés de **otro** server), y con **[P]** pegás el path a un `.json` puntual.
- **De qué vehículo es.** Te pregunta la **identidad del vehículo** (`Wheelbase`, `Fingerprint`), que la toma de v1 no sabe. Te lista lo que ya tenés: cualquier **grabación** de ese vehículo (el `header_*.txt` — sirve una de **10 segundos**) o una **toma ya calibrada** (esas además traen el freno medido). Pone primero la que coincide con el vehículo que declara tu ruta.
- **El perfil de obstáculos.** Te pregunta si Boris debe sortear autos parados en el camino: **Robusto** (frena + esquiva, ideal bus de línea 24/7), **Interceptable** (frena y se queda) o **Ninguno** (ver §A.5b).
- **Qué te corrige.** v1 marcaba la parada donde apretabas la tecla, no donde el vehículo frenaba: las paradas declaradas **en movimiento** se planchan a 0 con la frenada pintada hacia atrás; colapsa los puntos repetidos de cuando estabas detenido; marca el final como parada.
> Lo que **no** migra: el perfil de manejo es por vehículo. Si tu recorrido es con otro auto, calibralo igual — es el manual de manejo de *ese* auto, no de la ruta.

#### [6] Configurar paths — las carpetas del server
El wizard **recuerda** las carpetas en `wizard_config.json` (al lado del script) para no pedírtelas cada vez. En la **primera corrida** te las pregunta; después las cambiás acá cuando quieras *(Enter = mantener cada una)*:

| Carpeta | Qué es |
|---|---|
| **RoutesDir** | dónde van las **rutas** del server (el trío). Es tu `…\profiles\BZ_AutoDrive\`. **La única imprescindible.** |
| **Grabaciones del cliente** | de dónde lee tus `frame_/path_*.csv` (por default `%LOCALAPPDATA%\DayZ\BZ_AutoDrive_PathLogger\`). |
| **2º server (mirror B)** | si tenés un server de test aparte, te ofrece copiar la ruta allá al convertir. `-` = ninguno. |
| **Rutas v1** | dónde busca las tomas de BrigadaZ Transport v1 (para `[2]`). |

> Las carpetas son **portables**: un modder con el server en otro disco lo setea **una vez** y no toca `-RoutesDir` nunca más.

### Qué hace la conversión (y qué no)
Es una conversión **directa**: `frame_to_route.py` toma tu grabación + su `header_*.txt` (el fingerprint) y arma el trío — **generaliza** la línea leyendo la física del vehículo del config, y le pega la config de manejo. **No hay linters interactivos ni "puntaje"**: es un conversor puro, no te va a preguntar *"¿capeo esta curva?"*. La calidad sale de **grabar bien** (§7): si la toma quedó sucia (te subiste a un cordón, frenaste mal), **re-grabá** — es gratis y rápido.

> **La comparación visual** —tu traza 🔵 contra la de Boris 🟠, con los "puntos calientes" donde más se desvió— ahora se hace en el **editor**: importás la corrida de Boris (`boris_native`) como capa y la superponés sobre tu toma. Ver §6B (*Importar corrida/tramo*) y §16.2.

---

## 6B. El editor de trayectorias y mapas — *dibujá en vez de grabar*

Además de grabar manejando, podés **dibujar la ruta directamente sobre el mapa** — sin subirte a ningún vehículo. Es una herramienta de navegador (un HTML **self-contained**, no instala nada) que abrís con **doble clic en `tools\editor\trajectory_editor.html`**. Sirve para dos cosas distintas, y por eso tiene **dos modos** (el switch arriba):

> 🚗 **Trayectoria** — dibujás/editás **la ruta de un vehículo** (la que sigue Boris).
> 🗺 **Mapa** — editás **la red vial del mapa** (los caminos que usa el ruteo). *Esto es de admin/autoría.*

> **Todo es en vivo:** lo que dibujás o borrás queda **activo al instante** (se guarda en el navegador). El **Exportar** es solo para **desplegar/publicar**, no para trabajar.

**Mapas disponibles en el editor.** Es **un solo editor** (`trajectory_editor.html`) con el **fondo ya construido** (relieve, vegetación, edificios, huellas), que toma el mapa por parámetro. Para abrirlo en cada mapa, abrí el archivo que corresponda:

| Mapa | Abrí | Qué es |
|---|---|---|
| **Chernarus** (`chernarusplus`) | `tools\editor\trajectory_editor.html` | el editor (Chernarus es el default) |
| **Livonia** (`enoch`) | `tools\editor\trajectory_editor_livonia.html` | acceso → abre el editor con `?map=enoch` |
| **Sakhal** | `tools\editor\trajectory_editor_sakhal.html` | acceso → abre el editor con `?map=sakhal` |

> Los tres abren el **mismo** editor: los de Livonia/Sakhal son **accesos directos** de 2 líneas que le pasan el mapa. Para sumar un mapa nuevo alcanza con otro acceso igual.

> Se irán **agregando más mapas** en próximas actualizaciones (el pipeline de extracción es genérico — lee el tamaño y la superficie del propio scan, no hay nada hardcodeado por mapa).
>
> **⚠️ Ojo — esto es solo el FONDO del editor, no una limitación del framework.** El vehículo **maneja en cualquier mapa**: en el juego el NPC lee la **superficie roadway real** en vivo, así que **grabar y correr rutas funciona en todos lados**. Que un mapa todavía no tenga fondo dibujable acá solo significa que, por ahora, en ese mapa **grabás** la ruta (NUMPAD 5) en vez de dibujarla a mano en el editor.

### 🚗 Modo Trayectoria — armar una ruta a mano

**Las herramientas** (panel *⚒ Herramientas*):
- **✎ Pluma** — agrega nodos al final, curva. **╱ Línea** — igual pero recto.
- **⭶ Cursor** — mové un punto (con sus *handles* de curva). **▚ Selección** — caja para agarrar varios y mover/rotar el grupo. **＋ Insertar** — clic sobre la línea agrega un nodo ahí. **🗑 Borrar nodo**.
- **📏 Regla** — medí en metros (punto a punto y suma); doble clic o Esc para limpiar.
- **🖌 Pincel vel.** — pintá la velocidad (el valor del panel) sobre los nodos. **↹ Reverse** / **★ Evento** — marcás un tramo de marcha atrás o un nodo de evento (§10), igual que las teclas al grabar.

**El menú 🛣 Trayectoria** — de dónde sale la traza:
- **📄 Cargar mi toma (wp.csv)** — abrí una grabación **ya convertida** y editala (tocar velocidad, corregir la línea).
- **⏺ Importar corrida/tramo** — traé un **`boris_native`** para **COMPARAR** (entra como capa: tu traza 🔵 vs la de Boris 🟠, ves dónde se desvió — §16.2), o un **`frame_`** para **COMBINAR** con tu traza.
- **⟿ Traza auto (por calles)** — marcás **inicio y destino** y el editor **rutea solo por los caminos del mapa** (el grafo, §11). Ideal para armar un viaje largo sin dibujar cada tramo.

**El menú ⚡ Ruta** — el vehículo y su velocidad:
- **🚗 Asignar vehículo** — quién corre la ruta.
- **⚡ Recalcular Vel. óptima** — calcula la velocidad **óptima según la física del vehículo** (grip en curvas, freno, radio mínimo). Punto de partida para ajustar a mano.
- **✔ Validar ruta** — te marca **curvas imposibles, velocidades inalcanzables, frenados/aceleraciones imposibles** para ese vehículo (al panel ⚠), *antes* de correrla.

**Las capas** (*👁 Vista*) son el **fondo propio del framework** (no usa assets del juego): **Relieve** (medido, hillshade + altura), **Vegetación** (bosque/campo), **Edificios** (huella real coloreada por tipo), **Huellas** (tierra). Prendés/apagás lo que te ayude a dibujar.

**El proyecto** se guarda como `.bzproj.json` (📁 Proyecto → Guardar, `Ctrl+S`). Cuando la ruta está lista, **exportás los 3 archivos del framework** → deploy → play, igual que una grabación.

### 🗺 Modo Mapa — editar la red vial *(admin / autoría)*

Sirve para **armar o corregir la red de caminos** que el grafo (§11) usa para rutear — agregar una calle que la extracción automática no tomó, o borrar una que quedó mal.

- **Tipo de camino** (el que vas a agregar): **🛣 Carretera** (asfalto, ancha, con **carriles** — doble mano, se maneja por el derecho) · **🌾 Camino de tierra** (media, solo eje) · **🥾 Senda** (angosta, solo eje). *Regla a ojo: ¿carriles? carretera. ¿solo eje? tierra si es media, senda si es finita.*
- **✎ Pluma** para dibujar → **➕ Agregar traza a la red vial** con el tipo elegido → queda **ruteable al instante** y persiste.
- **🧽 Borrar camino (clic)** — cada clic elimina el camino que tocás, **incluidos los que vienen por default** (es una capa de exclusión, no toca el archivo base). **↺ Restaurar borrados** los devuelve.
- **⬇ Exportar caminos (planchar)** — arma el `.js` **final** para publicar (agregados + borrados). **Sin post-proceso**: lo que baja el navegador se copia y listo.

> **Undo/redo son por modo** (`Ctrl+Z` / `Ctrl+Shift+Z`): deshacer en Mapa no te mueve la trayectoria y viceversa. Idioma ES/EN con el botón 🌐.

---

## 7. Cómo grabar bien (la calidad sale de acá)

El NPC va a manejar **tan bien como manejaste vos** — la grabación es su manual de manejo. El wizard te avisa de los problemas, pero **no puede inventar una buena línea**: esa la ponés vos al grabar. Estos hábitos hacen la diferencia:

- **Manejá suave y parejo.** Nada de acelerones ni frenazos secos: el NPC copia tu perfil. Frená **progresivo y antes** de la curva, no encima.
- **Tomá las curvas con la línea que querés** que haga el NPC — ni cortes el vértice ni te abras de más.
- **Ritmo constante en las rectas:** la velocidad que grabás es la referencia que va a usar el NPC.
- **Una toma limpia vale más que diez sucias.** Si te subiste a un cordón o frenaste mal, **re-grabá** — es gratis y rápido.
- **Cambios de sentido (K-turn, galpón):** frená del todo, **cambiá de marcha y seguí** — el **intercambio se auto-detecta** del cambio de gear (no tocás ninguna tecla); la reversa también sale del gear (§9.4).
- **Paradas:** frená del todo y marcá con **NUMPAD 4** donde querés que el NPC pare.
- **Bocina y luces:** mientras grabás, **lo que hagas con la bocina (tecla H) y las luces (tecla L) queda grabado** y Boris lo reproduce **en el mismo lugar** donde lo hiciste (replay espacial). ¿Querés que el bus toque bocina al llegar a la parada? Tocá **H** ahí mientras grabás. ¿Querés que prenda las luces al entrar a un túnel? Tocá **L** ahí. No configurás nada — manejás como querés que se vea. *(Si preferís controlarlo por config en vez de grabarlo, ver los modos `HornMode`/`LightsMode` en el Apéndice A.2.)*
  > **Ojo con las luces:** por default `LightsMode` es **`auto`** (luces de noche automáticas — ver §7 "Bocina y luces"), así que **las teclas L que grabes solo se reproducen si ponés `"LightsMode": "replay"`** en la ruta. Con el default `auto` las luces las decide la hora del juego, no lo grabado. (La bocina sí se reproduce siempre por default.)
  > **Para que las luces se vean:** el vehículo necesita **batería + bombillas (faros) instaladas**. El framework le energiza la batería al spawn, pero las bombillas tienen que estar entre sus `Attachments` (normalmente ya vienen del fingerprint del vehículo). Sin faros físicos, la tecla L no prende nada.

> *Regla mental:* manejá pensando "así quiero que lo haga el bot". Lo que vos hagas, lo hace él.

---

## 8. El uso más simple: un servicio de bus permanente

Es el caso para el que **nació** el framework: **un bus de línea que recorre tu server solo, todo el día, sin que vos hagas nada.** Sin misiones, sin admin operándolo — transporte ambiental para tus jugadores. (Es el uso "solo para el server", el más común al bajar el mod.)

Cómo dejarlo corriendo como servicio permanente:
1. **Grabá una ruta en loop** (que vuelva cerca del inicio) — ej. la costa parando en cada pueblo.
2. **Dejala como la ruta default** del server (`BZBusRoute.json`).
3. Al arrancar el server, el bus **aparece solo y empieza a manejar**. Cuando termina la vuelta (o si lo destruyen), **respawnea** a los `RespawnDelay` segundos (`300` por default) → **servicio continuo, 24/7**.

> No necesita Quest ni que vos estés conectado: solo el mod cargado + tu ruta default. Lo prendés y se olvida.
> *Ejemplo:* el bus de la costa de Chernarus uniendo 14 paradas, dando vueltas todo el día — los jugadores lo toman para moverse, como un colectivo de verdad.

**Las tres formas de correr una ruta** (para tenerlas claras):
| Forma | Cómo arranca | Para qué |
|---|---|---|
| **Servicio permanente** | sola, al boot del server (ruta default + loop) | bus de línea ambiental |
| **On-demand (Reproductor)** | la elegís en la UI, sin restart | probar, eventos puntuales |
| **Por misión (Quest)** | la dispara un quest | convoyes, refuerzos, emboscadas (§12) |

### 8.1 — Parada a demanda: hacele señas al bus *(validado in-game)*

El bus de línea no para solo en los waypoints `isStop`: **cualquier jugador puede pedirle que pare haciéndole señas**, como en la vida real.

> **Viene desactivado por default** (opt-in — es una regla que el admin elige). Para habilitarlo, poné `HailGestureEnabled = true` en los *settings* globales del mod; sin eso, el bus solo para en los waypoints `isStop`. Mientras esté activo, la regla aplica a **todas** tus rutas.

**Cómo se usa (jugador):**
- Parate **en el camino, de frente al bus** (donde el chofer te ve), a ≤30 m.
- Hacé el emote **OK / pulgar arriba** (`ID_EMOTE_THUMB`).
- Boris **frena, te espera ~10 segundos** para que subas, y **sigue** la ruta.

**Qué pasa por dentro:**
- La detección es **server-side**: el framework recorre los jugadores cerca del vehículo, mira si están en el **cono frontal** del bus (producto punto rumbo-del-bus · dirección-al-jugador > 0.25 → "el chofer lo ve", no detrás ni al costado) y si están ejecutando el emote OK.
- Al detectarlo, **pausa** el bus (reusa el freno del modo pausa) y marca la reanudación a **+10 s**.
- A los 10 s reanuda **re-localizando** el waypoint a la posición real del bus (clave, ver abajo).

**Parámetros:** el **on/off** ya es config (`HailGestureEnabled`, default off); el resto está hoy en constantes (fáciles de exponer a JSON): radio 30 m · cono `dot > 0.25` (~±75°) · espera 10 s (20 ticks) · emote `ID_EMOTE_THUMB` (9).

**Dos cosas que costó resolver (notas para quien extienda):**
1. **Leer QUÉ emote hace el jugador, server-side.** `EmoteManager.GetGesture()` NO sirve: devuelve `m_GestureID` (otro campo, seteado por `SetGesture()`), no el emote en curso. El emote real vive en `m_CurrentGestureID`, que es `protected` y sin getter público. Solución: un `modded class EmoteManager { int BZ_CurrentGesture() { return m_CurrentGestureID; } }` — es script class (no engine → moddeable), y el `protected` es accesible desde la subclase.
2. **Retomar derecho tras la parada.** Al frenar, el bus sigue de largo ~10-15 m mientras se detiene, pero el índice de waypoint queda congelado donde frenó. Como el avance del índice está capado por la velocidad (no avanza desde 0 km/h), al reanudar el bus está *adelante* de su objetivo → el control apuntaba a un wp que quedó atrás → **timonazo al costado**. Fix: al reanudar, **re-localizar el índice al waypoint más cercano a la posición real** (una vez, salteando el cap).

> **El gesto como primitiva.** Es el primer uso de un patrón más general: *el emote del jugador como input de control del framework*. Hoy está cableado a "OK → parar", pero el mismo mecanismo (leer el emote server-side + condición de proximidad/visión) sirve para cualquier comando (seguir, esperar, dar la vuelta). Es la contraparte jugador↔NPC de los triggers del DSL de eventos (§10).

---

## 9. Cómo maneja el NPC — sigue tu línea y tu velocidad

**No elegís "modo": hay un solo control**, y es el que reproduce tu manejo. El NPC:
- **sigue tu línea** grabada (*pure-pursuit*: apunta a un punto adelante sobre tu traza), y
- **usa tu velocidad grabada** —vuela en las rectas, frena antes de las curvas, igual que vos—, **moderada** por lo que el vehículo **puede** hacer en cada curva: si grabaste una curva más rápido de lo que ese auto aguanta, la **capea sola**.

El acelerador/freno salen de un **modelo inverso** derivado del **config del vehículo** (motor, torque, caja), no de tus pedales. Por eso **el mismo controlador maneja cualquier vehículo sin tuning por-auto** (config-como-manual): cada uno lo hace según SU física — el chico clava la línea, el pesado la hace más lento pero llega. *(La grabación en sí es de su vehículo; para correr la misma **traza** en otro auto, la reasignás en el editor — §6B.)*

> **No configurás nada:** el wizard produce este control solo, leyendo el fingerprint del auto (§5). *(Antes existían "modos 1/2/3" —repetir pedales / velocidad por curvatura / velocidad grabada—; quedaron **unificados en este único control**, el que valida esta versión. Los flags que los prendían —`FollowPath`, `UseInverseModel`…— siguen en el Apéndice A.2 como **ajuste avanzado**, por si un modder quiere otro comportamiento.)*

> **¿Un vehículo pesado se ve forzado con tu ritmo?** Lo mejor es **grabar la ruta con ese vehículo** — así la corrida ya nace a su medida. *(Si después querés la misma traza para autos más chicos, dibujala/reasignala en el editor — §6B — en vez de reusar la grabación del pesado.)*

### 9.4 — Cambios de sentido (K-turn, entrar de cola): auto-detectados

Cuando tu ruta necesita **cambiar de dirección** —un giro de tres puntos (**K-turn**), dar la vuelta, entrar de cola a un galpón— **ya no marcás nada con una tecla**. El framework lo hace **solo**: detecta el **cambio de sentido** a partir del **cambio de gear (forward↔reverse)** que hiciste al grabar, y corta el tramo ahí (`legBreak`) por su cuenta.

**Cómo se graba — la regla: siempre DETENIDO al invertir.** No se puede pasar de forward a reversa (ni al revés) en movimiento —ni vos ni Boris—, y ese cambio de marcha **siempre cae a velocidad ~0**. Justo por eso el conversor lo puede leer sin ambigüedad. En un K-turn:
1. Venís grabando en **forward**. Llegás al punto donde vas a dar marcha atrás → **frená del todo** → metés la **marcha atrás**. *(No tocás ninguna tecla: el cambio forward → reversa es el intercambio, y el conversor lo detecta acá.)*
2. Hacés la reversa: **despacio (3–8 km/h), con correcciones chicas** (el arco no tiene que ser perfecto, lo vas corrigiendo). El framework **auto-detecta** que ese tramo es reversa —del gear— y lo trata como reversa.
3. Al terminar → **frená del todo** → metés **forward** y seguís. *(De nuevo sin tecla: el cambio reversa → forward es el segundo intercambio.)*

Repetís en cada cambio de sentido, siempre igual: **frenás del todo, cambiás de marcha, seguís**. Cada uno de esos cambios de gear corta el tramo (`legBreak`), y Boris lo trata como un **nuevo arranque**: llega alineado, se planta, y sale limpio en el sentido nuevo. Es el *cusp* (reversa↔forward), el punto más difícil del control — hoy **resuelto** (§18).

> **Un 0 km/h NO es siempre un intercambio.** Frenar del todo **sin cambiar de sentido** (esperar, ceder el paso) es una **pausa**, no un corte de tramo. El conversor distingue las dos cosas: solo el 0 km/h **con cambio de gear forward↔reverse** genera el `legBreak`. Por eso no necesitás marcar nada — el gear ya lo dice todo.

**Lo que el framework hace SOLO (no marcás nada):**
- **El intercambio** — sale del **cambio de gear** (forward↔reverse) grabado: ahí el conversor pone el `legBreak` y corta el tramo.
- **La reversa** — sale del **gear grabado**: si manejaste en marcha atrás, ese tramo es reversa. Boris la reproduce con su controlador de reverse (dirección por eje trasero) y la **generaliza a cada vehículo por su física** (uno de giro ancho reversa más lento y por eso sigue mejor el arco).
- **La aproximación** a la maniobra — **automática** (`ApproachAuto`): Boris frena predictivo ANTES de la maniobra, sin que marques nada.
- **La parada final** (endpoint) — autoadaptativa por vehículo + piso (§18).

> **Regla de oro: frená en la recta, no en la curva.** El punto donde frenás para invertir la marcha es donde el auto-detect **corta el tramo** — así que hacelo donde la **trayectoria YA ES RECTA** (el rumbo ya se estabilizó), no en plena curva. El detalle de por qué (los ángulos de volante de la reversa son del vehículo, no del camino) y la validación multi-vehículo están en §9.5.

> *Consejo (config-as-manual):* grabá la reversa **del lado seguro** — entrada lo más **recta** posible y mínimas correcciones. Cuanto más limpia tu demo, más limpio el NPC. En una **rampa cuesta arriba** (galpón) mantené el momentum: **no frenes a cero en la subida**.

> *Tomas que arrancan en reversa.* El conversor define la dirección del **tramo inicial** por el **primer movimiento real** (>1 km/h), no por el gear en que estás parado. Si tu ruta empieza dando marcha atrás, hacé que tu **primer movimiento SEA la reversa** —sin creepear ni un pelo hacia adelante— y largá decidido, así el arranque queda leído como reversa.

**Maniobras finas en el editor.** ¿Querés el arco EXACTO de un estacionamiento, sin depender del pulso? Lo dibujás en el **editor** (§6B) — da más control que grabarlo a mano (el editor marca el intercambio en el nodo, también sin tecla).

> **`maniobra` (legacy):** existió un modo `maniobra` (replay directo con salida por cruce de waypoint) que se marcaba con tecla. **Deprecado** desde 2026-06-17 y **sacado del menú**: cruise + aproximación automática + geometría lo cubren. No aplica a tomas nuevas.

### 9.5 — Que una traza con maniobra sirva en varios vehículos (dónde cortar el cambio de sentido)

La **reversa** es **replay directo (open-loop)**: el NPC reproduce los **ángulos de volante EXACTOS** que hiciste, **sin corrección lateral** (no hay cruise que lo acomode al camino). De ahí la premisa **"llegar listo"**: el vehículo tiene que alcanzar el tramo de reversa en la **pose y la velocidad** que demostraste — como es open-loop, un error de entrada no lo corrige.

**El punto fino: los ángulos de volante son del vehículo, no del camino.** Un mismo ángulo de volante da un **radio más ancho o más cerrado según el wheelbase** (un auto largo se abre, uno corto cierra). Entonces un tramo open-loop **sobre una curva NO generaliza** a otro vehículo: el que grabó cierra la curva, pero un auto más largo, con esos mismos ángulos, se abre y se la lleva puesta.

**La regla de oro — cortá en la recta, no en la curva.** Hacé el **cambio de sentido** (frená del todo antes de invertir la marcha, ahí el auto-detect corta el tramo) donde la trayectoria **YA ES RECTA** (el rumbo ya se estabilizó), no en plena curva. Así:
- La **curva** la maneja el **control cerrado** (el pure-pursuit sigue el **camino**, agnóstico al vehículo — cada auto la hace con SU volante).
- Solo la **recta + la maniobra** las hace el replay open-loop, que en recta es seguro (no hay curva que se abra).

**Cross-vehículo: el editor, no la grabación.** Una **grabación** es de **su** vehículo (captura su *fingerprint*, su freno medido, su marcha) — meterle un header ajeno trae problemas, así que **no se recomienda header-swapear grabaciones**. Para correr la **misma traza con maniobra en otro vehículo**, el camino de hoy es el **editor**: cargás la traza y le **asignás el vehículo** (o lo cambiás cuando quieras — "Asignar vehículo", §6B). La **traza dibujada es vehicle-independiente**, y ahí sí vale la regla del corte de arriba (la maniobra en recta). El framework, por su lado, **no reentrena nada por vehículo**: lee el config del auto asignado (motor, torque, caja — "config como manual de manejo") y arma su curve advisory y su modelo inverso a la medida de ESE auto. *(El header-swap de una grabación sigue existiendo como mecanismo técnico legacy, pero el camino recomendado es el editor.)*

> **Validado (la regla del corte).** Probando la **misma traza** en dos vehículos de wheelbase distinto —**OffroadHatchback** (2.357 m) y **CivilianSedan** (2.935 m, más largo)—:
> - con el **corte en la recta** → completó la maniobra sin chocar; la reversa clava el intercambio bien alineada (precisión de parada resuelta — §18).
> - con el **corte en la curva** → el Sedan, más largo, **se abrió y chocó** (esos ángulos de volante eran del Hatchback).
>
> Moraleja práctica: **para que una traza con maniobra sirva en varios vehículos, el corte va en recta** — y el cambio de vehículo lo hacés en el editor (§6B).

**La aproximación a la maniobra es automática (`ApproachAuto`).** El control frena predictivo antes del cambio de sentido, sin que marques nada. Si el corte cae en una **zona rápida** y notás que **frena de más** y se planta justo en la transición, la respuesta es **cortar la maniobra donde ya venís lento** (no en plena recta rápida) — así la entrada es suave.

**Sin teleport de alineación (snap OFF por default).** El *ModeEntrySnap* —el teleportito que alineaba pose+rumbo al entrar a la reversa— ahora viene **apagado por default**. El control genuino (reverse por eje trasero, que clava el rumbo a **<1°**) posiciona **sin teleportar**. Si alguna ruta puntual lo necesitara, se reactiva por config (`ModeEntrySnapEnabled` — Apéndice A.7).

---

### Bocina y luces — los modos `HornMode` / `LightsMode`

Aparte de **cómo** maneja, una ruta define **qué hace con la bocina y las luces**. La bocina por default **reproduce lo que grabaste** (replay espacial — §7); las luces, en cambio, **por default son automáticas** (ver abajo). Podés forzar otro comportamiento sin tocar la grabación, en el Apéndice A.2:

- **`HornMode`** — `replay` (default, toca donde lo hiciste vos) · `stops` (bocina en cada parada) · `finish` (bocina al llegar al final) · `off` (nunca).
- **`LightsMode`** — **`auto` (DEFAULT)** · `off` · `auto_inverted` · `replay` · `on` (detalle abajo).

**Luces automáticas por default (`auto`).** Desde la versión actual, **todas las tomas arrancan en `auto`**: de noche el vehículo **prende los faros solo al arrancar el motor** (apenas Boris se sienta y enciende, antes de empezar a rodar — no a mitad de ruta), y de día los deja apagados. **No hace falta grabar nada**: aunque la toma no tenga ninguna tecla L grabada, las luces se prenden solas de noche. El umbral de "noche" es la hora del mundo del juego: **luces ON de 19:00 a 06:00**, OFF el resto del día.

Los cinco modos de `LightsMode`:

- **`auto`** — *(default)* prende de noche (19:00–06:00) al arrancar el motor, off de día. Ignora lo grabado.
- **`off`** — luces **siempre apagadas**. Es el override para **misiones nocturnas sigilosas**: el vehículo no se delata con los faros aunque sea de noche.
- **`auto_inverted`** — al revés del `auto`: **apaga de noche** (sigilo por horario) y prende de día.
- **`replay`** — reproduce las teclas **L que grabaste** (prende/apaga exactamente donde lo hiciste vos — §7).
- **`on`** — luces **siempre encendidas**, de día y de noche.

> **Cómo DESACTIVAR las luces.** Si querés que una ruta corra **a oscuras** (misión sigilosa nocturna, no delatar el vehículo), poné en el header/JSON de la ruta:
> ```json
> "LightsMode": "off"
> ```
> Con `off` los faros nunca se prenden, aunque sea de noche y aunque hayas grabado teclas L. (Para volver al comportamiento automático, sacá el campo o ponelo en `"auto"`.)

> *Ejemplo:* un bus de pasajeros normal no necesita tocar nada — con el default `auto` ya prende de noche solo. Un convoy de infiltración usa `"LightsMode": "off"` (o `auto_inverted`) para no delatarse en la oscuridad.
> Recordá: para que las luces se vean, el vehículo necesita **batería + faros instalados** (§7).

---

## 10. Eventos y secuencias — que pasen cosas en la ruta

Una ruta no es solo manejar de A a B: podés hacer que **pasen cosas** en puntos definidos — el vehículo se detiene, baja gente, suena un audio, aparecen bots. Eso se arma con el marcador **NUMPAD 4** + los eventos.

### 10.1 — Tu primera secuencia, paso a paso (de cero)

*Objetivo: que el bus PARE en un punto, suene un mensaje, espere 3 segundos y siga.* Lo hacemos de principio a fin.

**Paso 1 — Grabá y marcá el punto.**
Manejás la ruta normal (NUMPAD 5 para empezar a grabar). Cuando llegás al lugar donde querés que pase algo (la parada), tocás **NUMPAD 4** *una vez*. Seguís manejando hasta el final y tocás **NUMPAD 5** para cortar.
> Cada NUMPAD 4 deja una marca en ese punto exacto. Podés marcar varios puntos en la misma vuelta.

**Paso 2 — Pasala por el wizard y anotá el número.**
Corrés el wizard → **Convertir** → elegís tu grabación. Te genera la ruta y, al convertir, te muestra una **lista de marcadores** con el **número de waypoint (wp)** de cada NUMPAD 4. Por ejemplo:
```
[ 1] wp 88   'parada'   dur=0s rad=0m  mode=normal
```
> **Anotá ese número (88).** Es la dirección de tu marca — la vas a usar para enganchar la acción.

**Paso 3 — Dónde quedó la marca en el archivo.**
Abrí el JSON de la ruta: `C:\DayZServer\profiles\BZ_AutoDrive\BZBusRoute.json`. Tu NUMPAD 4 quedó como un waypoint con **`"isStop": true`** y, en el ejemplo, es el **wp 88** (el índice 88 dentro de la lista `Waypoints`).
> *Cómo encontrarlo si no anotaste el número:* buscá `"isStop": true` en el archivo — ese es tu punto. Su posición en la lista `Waypoints` es el número de wp.

**Paso 4 — Enganchá la secuencia desde ahí.**
En el mismo JSON, agregás (o editás) el bloque `Events`, apuntando a ese wp:
```json
"Events": [
  {
    "wp": 88,
    "trigger": { "type": "wp_reached", "wp": 88 },
    "actions": [
      { "verb": "freeze_vehicle" },
      { "verb": "play_sound", "value": "MiVoz_SoundSet", "delay": 0.5 },
      { "verb": "resume_route", "delay": 3.0 }
    ]
  }
]
```
> El **`"wp": 88` es el enganche**: conecta tu marca (el NUMPAD 4) con lo que pasa ahí. Cambiá `88` por el número que te dio el wizard. Las `actions` se ejecutan en orden, con `delay` en segundos.

**Paso 5 — Deploy y probá.**
Guardás el archivo, deployás (el wizard lo hace, o copiás los `_hdr.json`/`_wp.csv`), y corrés la ruta desde el Reproductor. El bus llega al wp 88 → **frena → suena el mensaje → espera 3s → sigue.** Esa es tu primera secuencia. 🎉

> *Para más acciones (bajar bots, prender motor, etc.) y más triggers (al acercarse un jugador, al recibir daño), seguí leyendo — abajo está la lista completa.*

---

### 10.2 — Cómo funciona (referencia)

Cuando grabaste, tocaste NUMPAD 4 en los puntos importantes. Cada uno quedó como un **nodo de evento**. Después le colgás **acciones**: *"cuando el vehículo llegue acá, hacé esto"*.

Cada evento tiene dos partes:
- **Cuándo dispara** (el *trigger*): al llegar a un waypoint, cuando un jugador se acerca, cuando el vehículo recibe daño, a los X segundos del arranque…
- **Qué hace** (las *acciones*, en orden, con una demora opcional para coreografiar).

**Las acciones que tenés** (los "verbos"), por familia — **estos ya andan**:
- **Vehículo:** `start_engine` / `stop_engine` (arrancar/parar motor), `freeze_vehicle` / `unfreeze_vehicle` (clavarlo/liberarlo), `set_vehicle_mortality` (**switch de daño**: irrompible ↔ destructible, en caliente), `set_driver_mortality` (Boris mortal o no), `repair_vehicle`, `refuel` / `drain_fuel`, `lights_on` / `lights_off`, `horn`, `despawn_vehicle`.
- **Ruta:** `stop_route` / `resume_route` (pausar y reanudar el avance).
- **Carga:** `add_cargo` — mete ítems en el baúl (cambiás el **inventario del vehículo** en un punto de la ruta).
- **Gente:** `spawn_guard` (bots armados al costado), `dismount_guard` (que se bajen, animado).
- **Narrativa / estado:** `ui_broadcast` / `log_event` (mensaje), `play_sound` (audio 3D pegado al auto), `set_var` (**declarás o cambiás un estado** de la misión).

**Los triggers** (el CUÁNDO): `wp_reached` (Boris llega a un waypoint), `player_in_radius` (un jugador se acerca), **`player_enter_vehicle`** (un jugador **se sube** — el "onPlayerSit"), `vehicle_health_below` (le pegaron al vehículo), `timer` (a los X segundos del arranque).

> **⚠️ El evento NO obliga a parar.** Marcar un NUMPAD 4 **detenido** crea una **parada** real (`isStop`) — eso es lo que querés para un taxi. Pero un evento es solo *"trigger → acciones"*: podés colgarlo en **cualquier** waypoint (con ★ Evento en el editor, §6B) y que dispare **en pleno cruise, sin frenar** — ideal para **declarar estados** (`set_var`), prender luces, tocar bocina o soltar un mensaje al pasar. Y los triggers que **no** son `wp_reached` (`timer`, `player_enter_vehicle`, `player_in_radius`, `vehicle_health_below`) ni siquiera dependen de un punto: disparan estén donde estén.

**Ejemplos:**
- *Taxi (parada real):* en cada NUMPAD 4 colgás `stop_route` → `play_sound` ("subió un pasajero") → esperar 3s → `resume_route`. Para, levanta pasajero y sigue.
- *Arranca cuando alguien se sube (`player_enter_vehicle`):* el bus espera **con el motor apagado**; cuando un jugador se sienta, dispara `start_engine` → `resume_route` y **sale solo**. *(Ya existe como trigger — no hay que tocar código.)*
- *Cambiar el inventario en ruta (`add_cargo`):* al llegar al depósito, mete cajas/suministros en el baúl — sin frenar si lo colgás mid-cruise.
- *Volverlo frágil en la emboscada (`set_vehicle_mortality`):* viene irrompible; en el punto caliente lo hacés **destructible** para que el jugador pueda reventarlo.
- *Accidente (gancho de misión):* colgás `stop_engine` + `ui_broadcast` ("el bus se averió"). El jugador lo encuentra detenido.

*(El branching condicional —"si pasa X, hacé Y"— no vive acá; de eso se encarga el sistema de Quests, que se acopla aparte. Ver §12.)*

---

## 11. El grafo — armá tu propia red de rutas *(avanzado)*

Hasta acá, una grabación = una ruta. Pero podés grabar **muchas** (todas las calles de un pueblo, un pedazo de mapa, las vías de un tren) y el framework las **conecta en una red**. Después le pedís "andá de acá hasta allá" y te arma una ruta **que nunca grabaste entera**, combinando los tramos.

**¿Para qué?** Para que los NPCs vayan a cualquier punto de tu ciudad **sin grabar cada combinación**. Grabás las **calles**, no cada viaje posible.

**¿Cómo se conecta?** Solo: grabás un tramo, y donde cruza o roza otro se crea una intersección. Agregás una calle nueva y se suma a la red sin tocar las anteriores.

**¿Cómo lo armás?** En el **editor**, modo 🗺 **Mapa** (§6B): dibujás y borrás caminos, y exportás la red. *(No es el wizard.)*
1. Grabás las calles, cada una **ida y vuelta** (cada sentido = un carril → importante para que el NPC maneje **por la derecha**).
2. Un script arma la red a partir de las grabaciones.
3. Le pedís ir de **A a B** → te arma la ruta compuesta.
4. La deployás como cualquier ruta y la corrés.

> *Ejemplo:* grabaste 9 tramos de un pueblo (≈5 km de calles). Le pedís "de la terminal este a la esquina noroeste" → te arma un recorrido de 367m combinando 8 tramos, **que ningún humano manejó junto**. Probado en múltiples vehículos de distinta tracción y tamaño, con **96-99%** de precisión.

**¿Cuánto tengo cubierto?** Una herramienta de cobertura (`bz_coverage.py`) dibuja tu red sobre el mapa y te dice cuántos tramos, cuántos km y cuántas intersecciones tenés — así ves qué calles te faltan.

> *Las vías del tren* son el caso más fácil: la red **son** los rieles (sin volante, sin giros inventados — solo acelerar y frenar).

---

## 12. Misiones — BZ_AutoDrive + Quests *(la integración estrella)*

Esta es la pieza más potente del framework: que un vehículo manejado por NPC sea **parte de una misión** — refuerzos que llegan, un convoy que escapa, una emboscada en la ruta. Para eso, BZ_AutoDrive se acopla con **DayZ-Expansion-Quests**.

### 12.1 ¿Por qué dos sistemas? (la división de labor)

La clave es que **cada sistema hace lo que sabe hacer**:

- **Quest** = los **bots** + la **lógica de misión**. Spawnea bots vivos y armados, y es el dueño de todo lo "misional": matarlos da reward, progresión, condiciones de victoria/derrota.
- **BZ_AutoDrive** = el **vehículo**. Lo spawnea, lo **maneja** por tu ruta grabada, y coordina que los bots suban y bajen.
- **eAI** (la IA base de Expansion) = lo básico del bot: caminar, subir y bajar de un auto.

> *¿Por qué no hace todo el framework?* Un bot que spawnea el framework por su cuenta **no tiene lógica de misión** — matarlo no da nada (ni reward ni progresión). Y sin un Quest, los bots ni "viven" bien (aparecen como maniquíes). **Quest es el único dueño de bots armados con sentido.** Por eso se componen: Quest pone la gente y el sentido; vos ponés el vehículo y el manejo — la pieza que a Quest le falta.

### 12.2 Cómo funciona una misión, de punta a punta

1. El jugador **acepta el quest** (define dónde y cuántos bots).
2. Los bots **aparecen** cuando el jugador se acerca a la zona (son "lazy": se materializan por proximidad).
3. El framework **detecta** esos bots y **spawnea el vehículo** en el arranque de tu ruta.
4. Según el guión: los bots **suben** (caminando o ya sentados) y el NPC **maneja** la ruta.
5. Al llegar (o al dispararse un trigger), los bots **se bajan** — a desplegarse, a campear, a lo que pida la misión.
6. Cuando la misión termina, el vehículo se **limpia** (no queda dando vueltas).

### 12.3 Ejemplo 1 — "El convoy que huye" *(validado in-game)*

*La idea:* hay un campamento con 5 bots. Si el jugador empieza a matarlos, los sobrevivientes **se escapan en un vehículo**.

*Cómo se arma:*
- En el **quest**: un campamento con 5 bots armados en una terminal.
- En el **framework**: grabás la ruta de la terminal a un patio de escape, y la marcás como "convoy que huye al matar 1".

*Qué pasa en vivo:*
1. El jugador llega → los 5 bots aparecen y el vehículo (un Cobra) aparece solo en la terminal.
2. El jugador mata 1 → **se dispara la huida**: los 4 sobrevivientes **dejan de disparar** (se "pacifican"), **caminan** hasta el vehículo y **suben** con la puerta animada.
3. El NPC **maneja** hasta el patio.
4. Al llegar, **frena** y los 4 **se bajan** (puerta animada).

> El gatillo es el **kill-count del quest** (matar 1). El framework no inventa la lógica — *escucha* al quest y maneja el vehículo.

### 12.4 Ejemplo 2 — "Interceptá el convoy" *(emboscada)*

*La idea:* un convoy enemigo cruza la zona; el jugador lo embosca.

*Cómo se arma:*
- En el **quest**: 3 bots en el arranque del circuito.
- En el **framework**: una ruta de circuito, marcada como "emboscada al recibir daño", con el vehículo **destructible**.

*Qué pasa en vivo:*
1. Los 3 bots arrancan **ya a bordo, armados**, y el NPC los maneja por el circuito.
2. El jugador les pega un tiro → **cualquier daño** (al vehículo o a un bot) dispara la emboscada.
3. El vehículo **frena**, espera a detenerse del todo, y los bots (+ el conductor) **se bajan y campean** hostiles.

> *Insight de diseño:* esta emboscada es, en el fondo, un **marcador NUMPAD 4 generalizado** — un trigger ("al recibir daño") que dispara una secuencia ("frenar + bajar + combatir"). Hoy viene pre-armado como un modo; la dirección es que lo definas vos en los eventos (§10), sin tocar código.

### 12.5 El lifecycle del vehículo (no dejar autos fantasma)

En una misión, el vehículo **no debe quedar dando vueltas** al terminar. Cuando el quest marca los objetivos como completos, el framework **limpia** el vehículo (lo despawnea y corta el manejo). Otras políticas posibles: dejarlo como **botín**, dejarlo en sitio para un objetivo encadenado, etc.
> *Ejemplo:* en el convoy que huye, cuando el jugador finalmente liquida a los 4 en el patio, el quest marca "completado" → el Cobra se despawnea solo. No queda un auto fantasma en el mapa.

### 12.6 Lo que necesitás para armarla

- **Dependencia:** requiere **@DayZ-Expansion-Quests**. *(Para publicar el framework solo, esta parte se separa en un complemento opcional, así el core no obliga a tener Quests.)*
- **Tu parte (framework):** grabás la ruta del vehículo + la configurás con el modo de convoy y, si querés, los bots que viajan desde el arranque.
- **La parte del quest:** definís los bots y la lógica (reward, condiciones) en el editor de Expansion-Quests.
- **El enganche:** el framework *escucha* cuándo arranca el quest y dónde están sus bots, y desde ahí coordina el vehículo.

### 12.7 — Cómo se escribe (ejemplos de config)

La parte del framework se configura en el **JSON de la ruta** (`BZBusRoute*.json`). Acá van los ejemplos reales de las dos escenas. *(Los bots y el reward los definís aparte, en el editor de Expansion-Quests.)*

**Escena 1 — convoy que huye.** El vehículo espera en la terminal; cuando el quest detecta que mataste 1 bot, los demás suben y huyen:
```json
{
  "VehicleClass": "Star_APC_Cobra_white",
  "SpawnHoldSeconds": 600,
  "ConvoyMode": "flee_on_kill",
  "Crew": [],
  "Events": [],
  "Waypoints": [ /* tu ruta: de la terminal al patio */ ]
}
```
- `ConvoyMode: "flee_on_kill"` → activa la mecánica (pacificar → subir → manejar → bajar).
- `Crew: []` → vacío a propósito: **los bots los pone el quest**, no la ruta.
- `SpawnHoldSeconds: 600` → el vehículo espera quieto hasta que se dispare la huida.

**Escena 2 — emboscada.** Los bots arrancan a bordo armados; cualquier daño dispara el despliegue:
```json
{
  "VehicleClass": "x5mcompetition_orange",
  "ConvoyMode": "ambush_on_damage",
  "VehicleInvincible": false,
  "Crew": [],
  "Waypoints": [ /* el circuito */ ]
}
```
- `ConvoyMode: "ambush_on_damage"` → bots instant a bordo + freeze + dismount al recibir daño.
- `VehicleInvincible: false` → **clave**: si el vehículo es irrompible, nunca recibe daño y la emboscada **no dispara**.

**Bots que viajan desde el arranque (`Crew[]`).** Si querés que el vehículo nazca con bots adentro (sin depender del quest), los listás:
```json
"Crew": [
  { "cls": "eAI_SurvivorM_Boris", "seat": 1, "faction": "Raiders",
    "loadout": "BanditLoadout", "offsetRight": 2.0, "offsetForward": 0 }
]
```
- `seat`: 1+ (el 0 es el chofer). `faction`/`loadout`: cómo viene armado. `offsetRight/Forward`: dónde spawnea **afuera** antes de subir (que no quede dentro del cuerpo del auto).

**Una secuencia de eventos en un punto (`Events[]`).** Para colgar acciones a un nodo NUMPAD 4 — ej. una parada con sonido:
```json
"Events": [
  {
    "wp": 88,
    "trigger": { "type": "wp_reached", "wp": 88 },
    "actions": [
      { "verb": "freeze_vehicle" },
      { "verb": "play_sound", "value": "MiVoz_SoundSet", "delay": 0.5 },
      { "verb": "resume_route", "delay": 3.0 }
    ]
  }
]
```
- `trigger` = cuándo (acá: al llegar al wp 88). `actions` = qué, en orden, con `delay` (segundos) para coreografiar.
- Triggers disponibles: `wp_reached`, `player_in_radius` (con `radius`), `player_enter_vehicle`, `vehicle_health_below` (con `threshold` 0..1), `timer` (con `seconds`).

### 12.8 — El hook en código (cómo el framework "escucha" el quest)

La integración es server-side, en dos piezas. **(1)** Un `modded class MissionServer` que avisa al framework cuando arranca un quest:
```c
modded class MissionServer {
    override void Expansion_OnQuestStart(ExpansionQuest quest) {
        super.Expansion_OnQuestStart(quest);
        BZBusService.GetInstance().OnQuestStart(quest);
    }
}
```
**(2)** El framework guarda el ID del quest y **pollea**, porque los bots son *lazy* (aparecen recién cuando el jugador se acerca al campamento):
```c
void OnQuestStart(ExpansionQuest quest) {
    if (!quest) return;
    ExpansionQuestConfig qc = quest.GetQuestConfig();
    if (!qc) return;
    m_QuestCheckID = qc.GetID();                       // guardamos el ID
    GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.CheckQuestBots, 2000, true); // poll c/2s
}

void CheckQuestBots() {
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    bool exists = ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols);
    int totalBots = 0;
    if (exists)
        for (int i = 0; i < patrols.Count(); i++)
            if (patrols[i] && patrols[i].m_Group)
                totalBots += patrols[i].m_Group.Count();
    // totalBots > 0  -> el convoy se materializó (guardamos el conteo inicial)
    // el conteo BAJA -> mataron uno -> disparamos el trigger (huida / emboscada)
}
```
> **La puerta de entrada:** `QuestPatrolExists(questID, patrols)` te da las patrullas vivas del quest, y `patrol.m_Group` son los bots. Desde ahí el framework los alcanza — son las referencias que necesitás para subirlos al vehículo.

### 12.9 — Cómo embarca los bots (la mecánica + el gotcha)

Al dispararse el trigger, el framework agarra los bots vivos del quest y los sube. Lo no-obvio —y que costó horas— es que hay que **pacificarlos primero**: un bot *en combate* no camina a un waypoint (el FSM de eAI exige "sin amenaza"):
```c
void BoardQuestBots() {
    Transport transport = Transport.Cast(m_Bus);
    array<eAIQuestPatrol> patrols = new array<eAIQuestPatrol>();
    if (!ExpansionQuestModule.GetModuleInstance().QuestPatrolExists(m_QuestCheckID, patrols)) return;
    int seat = 1;
    for (int i = 0; i < patrols.Count(); i++) {
        eAIQuestPatrol p = patrols[i];
        if (!p || !p.m_Group) continue;
        for (int m = 0; m < p.m_Group.Count(); m++) {
            if (seat > 5) break;                          // Cobra: 5 plazas de pasajero
            eAIBase b = eAIBase.Cast(p.m_Group.GetMember(m));
            if (!b) continue;

            // 1) PACIFICAR (sino no caminan):
            b.eAI_SetPassive(true);
            b.eAI_SetThreatDistanceLimit(0.0);            // que no vuelva a fijar al jugador
            for (int tt = 0; tt < 16; tt++) {             // drenar los targets ya adquiridos
                eAITarget tg = b.GetTarget(0);
                if (!tg) break;
                b.eAI_RemoveTarget(tg);
            }

            // 2) Caminar a la puerta de SU asiento y subir (animado):
            vector door, ddir;
            transport.CrewEntryWS(seat, door, ddir);
            // ... SetMovementSpeedLimits + AddWaypoint(door); el Tick lo sube por la puerta ...
            seat++;
        }
    }
}
```
> **El gotcha que costó:** sin pacificar, el bot en combate no navega → terminaba subiendo por *teleport* (feo, "maniquí"). Con el combo `eAI_SetPassive(true)` + `eAI_SetThreatDistanceLimit(0)` + **drenar los targets**, el bot se "olvida" del jugador, **camina y abre la puerta** (animado, lindo). *Trade-off:* quedan pasivos durante la huida (coherente con que escapan).

> *Aún más detalle (todas las funciones del lifecycle, la lista completa de verbos, los gotchas de la API eAI) en el **AI knowledge pack**.*

---

### 12.10 — Ejemplo 3 — "Tomá el bus" (Travel) *(validado in-game)*

El tipo de objetivo más simple de integrar, y el **primero validado más allá de los convoyes**: un objetivo **Travel** (`ObjectiveType` 3) se cumple cuando el jugador **llega a una posición**. Si esa posición es el **final de una ruta** del framework, entonces **viajar en el vehículo cumple el objetivo**. El bus *es* el viaje.

**La pieza nueva: el campo `QuestTravelID`.** Una ruta declara qué quest la dispara. Cuando el jugador **reclama** ese quest, el framework auto-spawnea la ruta, espera `SpawnHoldSeconds` (para que llegues y subas) y maneja al destino. Cero intervención del admin — "reclamás y el bus aparece y anda solo".

**Lo que armás (todo config; del lado del quest no se toca código):**

**1.** El objetivo Travel (`Objectives\Travel\Objective_T_8.json`) — el destino = el final de la ruta:
```json
{
  "ObjectiveType": 3,
  "ObjectiveText": "Viaja en el bus hasta la terminal.",
  "Position": [3322.8, 200.0, 13029.7],   // = ultimo waypoint de la ruta
  "MaxDistance": 30.0,
  "TriggerOnEnter": 1
}
```

**2.** El quest (`Quests\Quest_42.json`) que lo envuelve, con su giver (una pizarra) y su reward.

**3.** En la ruta (`BZBusRoute_..._hdr.json`), dos campos:
```json
{
  "QuestTravelID": 42,        // el quest que auto-spawnea ESTA ruta
  "SpawnHoldSeconds": 20.0    // el vehiculo espera 20s antes de arrancar (para que subas)
}
```

**El flujo in-game:**
1. Reclamás el quest en la pizarra.
2. El vehículo aparece en el inicio de la ruta y **espera 20s**.
3. Subís de acompañante.
4. Boris maneja al destino → entrás al radio → **objetivo cumplido**, cobrás el reward.

**El hook** (en `OnQuestStart`): al arrancar un quest, el framework escanea las rutas; si alguna declara `QuestTravelID` == ese quest, la carga (`LoadConfigFromPath`) y la spawnea (`RespawnBus`); el `SpawnHoldSeconds` de la ruta hace la espera. Es el **mismo molde** que se extiende después a Escort/VIP (el vehículo lleva al VIP) y Delivery (el vehículo lleva la carga).

### 12.11 — Ejemplo 4 — Escolta al VIP (AIVIP) *(validado in-game)*

El objetivo que **cierra** la integración: un **AIVIP** (`ObjectiveType` 9, "escoltá al VIP a un lugar"). Expansion spawnea un **VIP** (un NPC eAI, ej. `eAI_SurvivorF_Gabi`); el framework lo **sube a un vehículo** y Boris lo maneja al destino, mientras vos lo **escoltás** (lo protegés en el camino). El VIP llega → objetivo cumplido.

**Dos NPCs distintos (no confundir):**
- **Boris** = el chofer (siempre el del framework).
- **El VIP** = el NPC del quest, el pasajero. Vos no manejás: protegés que llegue vivo.

**La pieza nueva: el campo `QuestEscortID`.** Igual que `QuestTravelID` (auto-spawnea la ruta al reclamar el quest), pero además **sube al VIP**:
```json
{
  "QuestEscortID": 10,         // el quest AIVIP que dispara esta ruta
  "SpawnHoldSeconds": 35.0,    // generoso: el VIP debe spawnear, caminar y subir ANTES de arrancar
  "VehicleClass": "CivilianSedan"
}
```

**El insight clave — cómo se agarra al VIP.** Acá hubo que leer el código de Expansion: el VIP **NO** está en `QuestPatrolExists()` (esa lista es solo de AICamp/AIPatrol). Está **flageado con `Expansion_IsQuestVIP()`** y sale del enum global **`eAIBase.eAI_GetAll()`**. El framework escanea esa lista, filtra por el flag + proximidad al vehículo, y lo embarca en su propio grupo (sin tocar el grupo del jugador). *(Técnica útil: las `.c` de Expansion se leen como texto adentro del PBO — no hace falta extraer.)*

**El flujo in-game:**
1. Reclamás `Quest_10` en el NPC (Denis, al lado de la pizarra).
2. Aparece el **CivilianSedan + Boris** en el inicio de la ruta y espera 35s.
3. **El VIP** spawnea cerca → el framework lo encuentra (`eAI_GetAll` + `Expansion_IsQuestVIP`) → lo sube caminando.
4. Boris lo lleva al destino (fin de ruta) → baja → **objetivo cumplido** (con `Autocomplete: 1`, se finaliza solo).

**Gotchas que aprendimos (cuesta caro saltarlos):**
- **La grab NO es `QuestPatrolExists`** — para AIVIP es `eAI_GetAll()` + `Expansion_IsQuestVIP()`.
- **El vehículo importa:** tiene que tener asientos de crew válidos + **puertas vanilla**. Un carpack (probamos el X5M) se **hundió** (bbox raro) y su puerta **crasheó el FSM de eAI** (`OpenVehicleDoor`) → spam de errores → server sobrecargado. El **CivilianSedan** (vanilla, 4 puertas) anduvo perfecto.
- **Asiento del VIP:** mandalo a un **asiento trasero**, no al acompañante delantero — sino el jugador y el VIP se pelean el mismo asiento y se fusionan (te traba adentro del auto).
- **Hold generoso:** el VIP necesita tiempo para spawnear, caminar y subir antes de que el auto arranque (`SpawnHoldSeconds` ~30-40s).
- **Si la quest queda colgada** (cambiar la config de un objetivo con la quest activa la rompe): poné `Autocomplete: 1` + reiniciá — la quest cumplida se auto-finaliza.

**Lo que esto cierra:** con AIVIP validado, los objective types donde el vehículo tiene un rol — **AICamp/AIPatrol, Travel, AIEscort/VIP** — están todos andando. Los demás (Collection, Crafting, etc.) **coexisten** sin conflicto. Eso es la integración Quest completa.

## 13. Controles (las teclas)

El framework usa **solo 3 teclas** — el ciclo de grabación + abrir la UI. Todo lo demás (largar / parar / pausar un vehículo, spawnear vacíos, armar los loggers) se hace **desde el Reproductor** (§14), no con teclas. Las 3 viven en el **menú del juego**: `Opciones → Controles → sección "BZ AutoDrive"`, **rebindeables** como cualquier control de DayZ, y vienen **asignadas por default**:

| Acción (nombre en el menú) | Default | Para qué |
|---|---|---|
| **Open Control Panel** | `INICIO` (Home) | **abrir / cerrar** el Reproductor (la puerta a toda la UI) |
| **Record (start/stop)** | `NUMPAD 5` | empezar / terminar de grabar una ruta |
| **Mark Event / Stop** | `NUMPAD 4` | marcar una **parada / nodo de evento** (§10) mientras grabás |

> *El cambio de sentido ya no es tecla:* el **intercambio / K-turn** (§9.4) se **auto-detecta** del cambio de gear forward↔reverse — frenás del todo, cambiás de marcha y seguís; el conversor corta el tramo ahí y Boris lo trata como un nuevo arranque.

> *Flujo típico:* abrís el panel con `INICIO`; para grabar, `NUMPAD 5` (con `NUMPAD 4` en las paradas/eventos; en los cambios de sentido solo frenás, cambiás de marcha y seguís — se auto-detectan), `NUMPAD 5` de nuevo para cortar; después elegís la ruta en el Reproductor y mirás cómo le va. Tus asignaciones se guardan en tu perfil (`Documents\DayZ\<perfil>.dayz_preset_User.xml`).

> **⚠ Los controles los VE todo el mundo, pero solo funcionan para el admin.** DayZ le muestra la categoría "BZ AutoDrive" en Controles a **todos** los jugadores (registra los inputs de forma global — no se puede ocultar por admin). Pero las teclas **no hacen nada** si no sos admin: el gate es **server-side**, se valida en cada acción contra tu Steam ID (§14.1). Por eso el menú se dejó **mínimo** (3 teclas): un jugador común ve una categoría corta e inerte, no un tablero de debug.

> *Nota:* el `ai_run` (la caja negra de Boris) ya **no tiene tecla** — se arma desde el check del Reproductor (§14, §16.1). Las herramientas viejas de tuning/debug (Parking/Reverse/Approach en vivo, SysID, gear markers, spawn slots) se sacaron del menú; las maniobras salen del **intercambio auto-detectado** (cambio de gear) + el editor.

---

## 14. La interfaz (UI)

Un solo panel para el admin — el **Reproductor** — que lo abrís con una tecla (**INICIO/HOME** por default, configurable). Reúne todo en una pantalla: la lista de rutas, lo que se está manejando en vivo, y el spawn de vehículos vacíos. Se divide en tres zonas:

**① ROUTES (izquierda) — tu lista de rutas.** Todas las rutas guardadas. Hacés clic en una y **el NPC la maneja al instante, sin reiniciar el server**. Al seleccionarla, abajo te muestra su **ficha** (vehículo, cantidad de waypoints, distancia, velocidad máx) y un **scrubber** para recorrer la ruta wp por wp. Debajo de la lista, el botón **LOAD & SPAWN** (largar la ruta elegida) y los **checks de logger** (ver abajo).
> *Ejemplo:* tenés "Bus pueblo", "Taxi centro", "AB - Jeep" en la lista; clic en una y arranca.

**② ACTIVE RUNNERS (derecha, arriba) — tu tablero en vivo.** Cada vehículo que se está manejando **ahora** — tus rutas, las de misiones, las del arranque del server — con su **nombre + waypoint** y su **estado** (`DRIVING` / `PAUSED` / `STOPPED`). Por cada uno, cuatro botones: **RST** (reiniciar desde el wp 0 de su ruta, sin recargar), **TP** (teletransportarte al lado para interceptarlo), **II** (pausar / reanudar), **[]** (pararlo y sacarlo). Si hay muchos, la lista **scrollea** con la rueda. Arriba a la derecha, **STOP ALL** para bajar todos de una.
> *Ejemplo:* ves "Bus pueblo — wp 88/166 — DRIVING". Lo pausás con **II** o te TP al lado para subirte.

**③ ACTIVE SPAWN VEHICLE (derecha, abajo) — los vehículos vacíos que sembraste.** Cada vehículo que spawneaste vacío (con START/HERE/END, ver abajo) aparece acá como una fila —**ruta · wp · posición**— con dos botones: **TP** (ir a su lado) y **ELIMINAR** (borrarlo). Podés dejar **varios** vacíos sembrados por el mapa y manejarlos desde acá. Scrollea si hay más de los que entran.

**La barra de abajo — spawn de vehículos vacíos + scrubber.** Muestra la ficha de la ruta seleccionada + el scrubber, y a la derecha **SPAWN EMPTY VEHICLE AT: START · HERE · END**: spawnea el vehículo de la ruta **vacío y manejable** en el inicio, en el wp del scrubber (**HERE**) o en el final. Sirve para **continuar una grabación** (spawnás el auto donde quedaste y seguís manejando) o para sembrar autos. **Ya no te teletransporta** al vehículo: aparece en la lista **ACTIVE SPAWN VEHICLE** (③) y desde ahí lo TP o lo eliminás.

**Los checks de logger (opt-in, antes de dar play).** Debajo de la lista de rutas hay dos casillas — **`[ ] boris_native`** y **`[ ] ai_run`** — que **tildás ANTES de largar la ruta** (LOAD & SPAWN). Si están tildadas, esa corrida graba su trayectoria **en sincro con el play**:
- **`ai_run`** — la **caja negra** de Boris (telemetría de la corrida; ver §16.1). Se arma **desde este check** (ya no con tecla) para esa corrida puntual.
- **`boris_native`** — la **trayectoria server-side** de Boris (misma forma que una grabación humana), para **superponerla contra tu toma en el editor** y ver dónde se desvió (detalle en §16.2).
> Vienen **destildadas** (no graban nada). Es opt-in por corrida: tildás, das play, y solo esa corrida escribe el log. Sin tildar, uso normal, cero archivos.

> *Nota:* las teclas se rebindean desde el menú de Controles del juego (§13). Los checks arman los loggers desde la propia UI, sin depender de las teclas.

### 14.1 — ¿Quién es admin? — configurar `AdminSteamIDs`

Recién dijimos que los controles y las UIs **"solo tienen efecto para vos (admin)"**. ¿Pero cómo decide el framework quién es admin? Por tu **Steam ID**.

**Cómo te identifica.** El server compara tu Steam ID (el Steam64, también llamado *PlainId*) contra una lista llamada `AdminSteamIDs` en su archivo de settings. Si tu ID está en esa lista, sos admin.

**El archivo.** `<server>\profiles\BZ_AutoDrive\BZAutoDrive_settings.json`. Si no existe, lo creás vos. Los dos campos que te importan:
- `AdminSteamIDs` — lista de strings con los Steam IDs autorizados.
- `ControlPanelKey` — la tecla que abre el panel (`-1` = INICIO/HOME por default).

**⚠️ LO MÁS IMPORTANTE (esto es el punto de seguridad):** si `AdminSteamIDs` está **VACÍA, TODOS son admin**. Es cómodo para testing local, pero en un server **PÚBLICO TENÉS que poner tu(s) Steam ID(s)** ahí. Si no lo hacés, **cualquier jugador** puede abrir el panel, spawnear y controlar los vehículos. Es lo **primero** a setear antes de abrir el server al público.

**Ejemplo del JSON:**
```json
{
  "AdminSteamIDs": ["76561198000000000"],
  "ControlPanelKey": -1
}
```
Podés poner varios IDs separados por coma. `ControlPanelKey: -1` = tecla INICIO/HOME.

**Cómo encontrar tu Steam ID** (el Steam64 / *PlainId*): en tu perfil de Steam, en **steamid.io**, o en el **log del server** cuando te conectás.

**Por qué es seguro.** El chequeo se hace **server-side en cada acción** (abrir panel, spawnear, cargar ruta, parar…), comparando tu Steam ID autenticado por Steam. No se puede falsear desde un cliente modificado: el gate del lado del cliente es solo comodidad visual.

> *Nota honesta:* un jugador común igual puede **VER** la categoría "BZ AutoDrive" en su menú de Controles (DayZ le muestra a todos los inputs registrados), pero las teclas **no hacen nada** para él si no es admin.

---

## 15. Audio (.ogg) — ponerle voz a tus eventos

Con el verbo `play_sound` (en un evento NUMPAD 4) reproducís un audio **3D pegado al vehículo**. El audio lo ponés vos en tu addon:
1. Metés tu `.ogg` en tu addon.
2. Lo declarás en tu `config.cpp` (un `CfgSoundShaders` + un `CfgSoundSets` — copiás el patrón de ejemplo).
3. En el evento: `play_sound` con el nombre del SoundSet.

> *Ejemplo:* tu taxi, al levantar pasajero, dice "buen día" — grabás `buen_dia.ogg`, lo declarás, y lo disparás en el evento de la parada.
> *Tip:* para probar sin crear un OGG, usá un sonido que ya exista (ej. del mod de radio). *Ojo:* a veces un sonido lanzado desde el server no llega al cliente — si no suena, hay que reenviarlo por red (la infra ya está).

---

## 16. Si algo no anda

Algunos clásicos (el detalle fino + más casos están en el **AI knowledge pack**, ver abajo):
- **El vehículo aparece pero no se mueve** → casi siempre las coordenadas de la ruta no coinciden con el mapa (grabaste en otro mapa). Grabá tu propia ruta en el mapa donde corrés.
- **No se grabó mi corrida del NPC** → el check **`ai_run`** estaba destildado. Tildalo en el Reproductor *antes* de dar play (§14).
- **El NPC no entra a una curva cerrada** → es el tramo más exigente (§18); el control **capea la velocidad** solo, pero si el vehículo es **pesado**, lo mejor es **grabar la ruta con ÉL** (nace a su medida). Si es una maniobra cerrada de verdad (galpón, cambio de sentido), va como **reverse** — ver §9.4–9.5.
- **No suena el audio** → ver §15 (replicación server→cliente).

> **Este framework viene con un AI knowledge pack:** un documento pensado para pasarle a una IA (Claude/GPT/Gemini) que te asista a configurar tu caso, resolver errores y armar tus rutas. Si te trabás, esa es la vía rápida — la IA tiene todo el contexto del framework cargado.

> **Tip de performance — correr el server en otra PC.** Si iterás mucho (grabás, convertís, probás), conviene **correr el server en una 2da PC** mientras tu PC principal corre el **cliente del juego + el wizard y las herramientas**. Así ninguna de las dos se ahoga y las corridas salen más fluidas (mejor para calibrar). El **deploy** a ese 2do server es: *buildear* el mod → *copiar* el `@BZ_AutoDrive` al 2do server → *sincronizar las rutas* (los JSON). El wizard ya soporta apuntar a ese 2do server: configurás su carpeta en **[6] Configurar paths** y te ofrece copiarte las rutas ahí al convertir.

---

### 16.1 — AI logger — la caja negra de Boris (ai_run)

Cuando una ruta **no sale como esperabas** y querés saber *por qué*, el framework te deja grabar una **caja negra** de la corrida del NPC. Funciona como el *flight recorder* de un avión: registra, segundo a segundo, lo que Boris hizo mientras manejaba, para que después puedas diagnosticar el problema.

**Qué es.** Un **log de telemetría** de las corridas de Boris. No cambia cómo maneja: solo **observa y anota** (posición, velocidad, marcha, qué pedales apretó, qué modo usó). Es la materia prima para entender un comportamiento raro.

**Es OPT-IN, viene apagado.** Se arma **tildando el check `ai_run` en el Reproductor ANTES de dar play** (§14, ya **no con tecla**) → se graba en sincro con esa corrida puntual. **Si no lo tildás, no se escribe nada.** No es parte del uso diario: un admin que solo quiere sus buses o convoyes andando **no lo necesita**.

**Cuándo usarlo.** SOLO cuando una ruta se porta mal —Boris **se abre en una curva**, **se traba**, **va lento**— y querés entender **por qué**, o cuando estás **afinando el config a mano**. El resto del tiempo, dejalo apagado.

**Dónde vive.** En el **server que corrió a Boris** (no en tu cliente): `<server>\profiles\BZ_AutoDrive_PathLogger\ai_run_*.csv`. *Ojo:* si probaste la ruta en otro server, el `ai_run` queda en **ESE** server — buscalo en la máquina donde corrió.

**Qué registra.** Unas **~27 columnas de telemetría**: la **posición** y el rumbo, la **velocidad real y la objetivo**, el **desvío lateral** (ya calculado), los **inputs** que aplicó (`throttle`, `brake`, `steering`), las **rpm**, la marcha, el **modo activo**, los **marcadores de evento**, y el **waypoint que persigue** (`wp_idx`) en ese instante.

> **⚠ Principio importante — que quede clarísimo:** el `ai_run` es una **MEDICIÓN para calibrar CONTRA tu toma** (un *feedback* funcional de cómo le fue a Boris), **NO una grabación nueva**. **No se lo des al wizard como si fuera una toma** — de hecho el wizard los **filtra a propósito**. ¿Por qué? Porque realimentar la salida de Boris **clona sus propios errores**: la ruta se degrada generación tras generación (es el *model collapse* de Machine Learning — la fotocopia de la fotocopia). La fuente de verdad es siempre **tu** grabación humana; el `ai_run` solo sirve para **medir el desvío** contra esa verdad.

**Cómo sacarle provecho.** Dos caminos:
- **Con IA (recomendado):** le pasás el `ai_run` **junto con la ruta** a una IA con el **AI Knowledge Pack** cargado, y te lo diagnostica — dónde Boris sufrió y qué ajustar (qué campo del config tocar).
- **A mano (si sos técnico):** lo abrís (es un CSV) y mirás vos mismo dónde **se desvió**, **se trabó** o **perdió velocidad** — cruzándolo contra tu toma original.

> *Ejemplo:* el bus se abre siempre en la misma curva. Tildás el check **`ai_run`**, corrés la ruta, agarrás el `ai_run_*.csv` del `profiles` del server, se lo das a la IA con la ruta → te dice *"en el wp 340 el `steering` se satura (llega al tope sin alcanzar el giro): subí `CurvatureSteerBoost`"*. Ajustás eso, **no** realimentás el `ai_run` como toma.

---

### 16.2 — boris_native — la trayectoria de Boris (para comparar en el editor)

Aparte del `ai_run` (la telemetría de diagnóstico), el framework puede grabar la **trayectoria** de la corrida de Boris: por dónde pasó, a qué velocidad, con qué rumbo — en el **mismo formato que una grabación humana**. Se llama **`boris_native`**.

**Qué es.** Un CSV con la línea que Boris **realmente manejó** (posición, rumbo, velocidad, `throttle`/`brake`/`steering`, marcha, ángulo de rueda), server-side, a ~40 Hz. Como está en el mismo formato que tu `path_`/`frame_`, se puede **cargar en el editor de trayectorias** y **superponerlo contra tu toma** para ver, de un vistazo, dónde Boris siguió tu línea y dónde se abrió.

**En qué se diferencia del `ai_run`** — la distinción clave:
- **`ai_run`** = **telemetría de diagnóstico** (desvío lateral, saturaciones, corredor, pedales) → responde **"por qué"** Boris hizo algo raro. Se lee con la IA / a mano (§16.1).
- **`boris_native`** = la **trayectoria cruda** (la línea + velocidad, en formato de grabación) → responde **"por dónde"** fue → se **dibuja/superpone** en el editor contra tu toma.

> Uno **mide**, el otro se **dibuja**. Para **diagnosticar** mirás el `ai_run`; para comparar la línea a ojo, cargás el `boris_native` en el editor sobre tu grabación.

**Es OPT-IN.** Se arma **tildando el check `boris_native` en el Reproductor ANTES de dar play** (§14). Vive en el **server que corrió a Boris**: `<server>\profiles\BZ_AutoDrive_PathLogger\boris_native_*.csv` (uno por corrida, con timestamp — nunca se pisa).

> **⚠ Igual que el `ai_run`: es MEDICIÓN, no una toma nueva.** No se lo des al wizard como grabación — realimentar la salida de Boris **clona sus errores** (§16.1, *model collapse*). Sirve para **comparar** contra tu grabación humana (la fuente de verdad), no para regenerar la ruta.

---

## 17. Ideas — qué podés construir (y qué se podría sumar)

Framework + Quest abren un espacio grande. Estas ideas son para inspirarte: algunas las armás **hoy** con lo que hay; otras son **extensiones** para sumar (verbos/triggers nuevos). La regla: *si lo podés describir como "un vehículo que va de acá para allá y en el camino pasa X", probablemente lo podés armar — o sumar el verbo que falta.*

### Arquetipos que armás HOY
- **Transporte público** — bus/taxi con paradas y eventos (§10).
- **Convoy** — refuerzos, escolta, huida, emboscada (§12).
- **Patrulla motorizada** — un vehículo en loop por una zona.
- **Logística / supply run** — un camión que lleva loot (`add_cargo`) a un punto y lo deja.
- **Rescate / extracción** — el vehículo llega, sube bots o jugadores, los saca.
- **Evento ambiental** — un auto que cruza el pueblo y se va; vida en el mundo, sin misión.

> *Ejemplo combinando:* misión **"protegé el convoy"** — un camión del framework hace la ruta, el quest pone enemigos que lo emboscan, y el jugador tiene que defenderlo hasta el destino.

### Acciones que se podrían sumar *(extensión del DSL de eventos)*
- **Bloquear / expulsar asientos** (`lock_seat` / `unlock_seat` / `eject_passenger`) — que **los jugadores no puedan subirse** (o bajarlos). El campo `slot` ya está **reservado** en el DSL (`BZAction`); falta escribir el handler en `ExecuteAction`.
- **Cambiar la facción** de los ocupantes en runtime (`set_faction`) — que Boris o su crew cambien de bando a mitad de misión (de neutral a hostil, p. ej.).
- **Cambio de chofer** (`swap_driver`) — reemplazar al conductor en un punto (baja Boris, sube otro; o pasa a manejar un bot del quest).
- **Gestos** — saludo, venia, señalar (en el roadmap; todavia sin implementar).
- **Balizas / luces de emergencia** — balizas en una avería. *(La **bocina y las luces** comunes ya andan: se graban y se reproducen — ver §7 y los modos `HornMode`/`LightsMode` del Apéndice A.2.)*
- **Soltar loot / abrir baúl** — dejar un ítem en un punto; abrir el baúl al llegar.
- **Llamar refuerzos** — spawnear *otro* vehículo del framework (cadenas de convoy).
- **Marcador en el mapa** — que el vehículo aparezca en el mapa del jugador (seguir el bus en vivo).
- **Dar / avanzar objetivo de quest** — que llegar a un punto **complete un objetivo** (el framework como motor de objetivos).

### Triggers que se podrían sumar
- **Por ocupación** (`occupancy >= N`) — que el vehículo espere a **llenarse** (N pasajeros/bots sentados) antes de arrancar. Hoy `player_enter_vehicle` ya dispara al subir **el primero**; falta el umbral de "cuando estén los N". Combinado con `start_engine` + `resume_route` = el bus sale **cuando se completa el cupo**.
- **Al disparar** (`on_player_shoot`), **de noche**, **por cantidad de jugadores** en la zona, **al entrar a un área**.

### Combos framework + Quest *(el espacio grande)*
- **Taxi que es quest-giver** — te subís, te lleva, y en el viaje te ofrece una misión.
- **Jefe que llega en vehículo** — el boss aparece manejando, baja y pelea.
- **Trader móvil** — un vendedor recorre una ruta; los jugadores lo paran para comprar. *(Necesita integración con Market — idea parqueada.)*
- **Eventos dinámicos** — spawns aleatorios de vehículos + rutas, para que el mundo se sienta vivo.
- **El framework como motor de objetivos** — que los eventos del vehículo (llegó / fue destruido / entregó) sean las **condiciones de completion** de un quest. *Este es el norte del proyecto.*

### Framework + cada tipo de objetivo de Quest

La integración se validó in-game con **AICamp/AIPatrol** (los convoyes, §12), **Travel** (§12.10) y **AIEscort/AIVIP** (§12.11) — los tipos donde el vehículo cumple un rol. Los objetivos sin vehículo (Collection, Crafting, etc.) coexisten sin conflicto. La tabla completa:

| Objetivo de Quest | Rol del vehículo (BZ_AutoDrive) | Ejemplo |
|---|---|---|
| **AICamp / AIPatrol** ✅ | trae/saca los bots, los embarca y maneja | el convoy que huye, la emboscada (§12) |
| **AIEscort** ✅ | el vehículo **es lo que se escolta** | "acompañá el convoy de Boris hasta la base"; si llega entero, cumplido |
| **AIVIP** ✅ | el VIP **viaja en** el vehículo | "protegé al VIP" mientras el NPC lo lleva a zona segura |
| **Travel** ✅ | el vehículo **te lleva** | "viajá a Cherno" se completa subiéndote al bus que va para allá |
| **Delivery** | el vehículo **hace la entrega** | supply-run: el camión NPC lleva la carga al punto; el objetivo es que llegue |
| **Target** | el target **va en** el vehículo | "eliminá al jefe" que se escapa en el convoy → lo perseguís |
| **Action / Collection** | el vehículo **deja algo** o **es el lugar de la acción** | inspeccionar el accidente, reparar el camión, recoger el loot que soltó |

> *Lo validado:* el framework "escucha" el quest y aporta el vehículo, sea cual sea el objetivo. Ya andan in-game los **tres ejes donde el vehículo cumple un rol** — **AICamp/AIPatrol** (convoyes, §12.3/§12.4), **Travel** (§12.10) y **AIEscort/AIVIP** (§12.11). Los demás (Delivery, Target, Action/Collection) **reusan el mismo hook** con su propio campo declarativo: el enganche ya existe, falta el caso de uso.

> *Para modders:* si se te ocurre una acción o trigger que no está, casi siempre **encaja en el DSL existente** (un verbo nuevo en `ExecuteAction`, un trigger nuevo en la lista). El AI knowledge pack te muestra cómo agregarlo.

---

## 18. Alcance y límites (qué esperar)

Para que sepas qué pedirle al framework y qué no:

- **Régimen nominal:** maneja al límite *normal* del vehículo. Maniobras extremas (derrapes, sobreviraje fuerte, contravolante) **no** las reproduce con fidelidad — el receptor del motor no las entrega. Eso queda fuera de alcance, y está bien: el framework apunta al manejo realista, no al stunt.
- **Alta velocidad:** anda, con **degradación graceful**. Probado hasta ~190 km/h: la línea se afloja un poco con la velocidad (de ~0.2m parado a ~0.8m a +120 km/h) pero **no se rompe** (sin oscilación, 99% dentro de 2m).
- **Curvas de 90° cerradas:** es el punto más exigente. Salen, pero ahí se nota el vehículo — uno pesado se abre más que uno chico.
- **El CONTROLADOR generaliza** sobre un **rango amplio de R_min** (de un hatchback chico a un camión) con **una sola config**: la misma traza sirve para todos y el matiz lo pone la física de cada uno.
- **Maniobras (parking / reversa / K-turn) en varios vehículos:** el cross-vehículo se hace en el **editor** —asignás el vehículo a la traza (§6B, §9.5)— y funciona **siempre que el corte de la maniobra caiga en recta, no en curva** (validado con la misma traza en OffroadHatchback y un CivilianSedan más largo, parking 0.71 m). El **intercambio reversa→forward** —el punto más exigente del control, un K-turn adentro de un galpón— quedó **resuelto y validado en un banco amplio** (Golf FWD, BMW E60, OffroadHatchback, Hatchback FWD, Porsche GT2RS motor-central, Sedan, Toyota 86 RWD y el **camión Truck_01**): **0 rescates de AutoRecovery**, con reglas físicas y **sin constantes por-vehículo**. El intercambio se **auto-detecta del cambio de sentido** (no hace falta marcarlo — §9.4).
- **Precisión de parada (endpoint) — autoadaptativa:** el freno de parada **ya no usa constantes ni valores por-ruta**; se autoconfigura por **vehículo + piso** — lee el torque de freno del config del auto y el **agarre real de la superficie** bajo Boris (asfalto, tierra…), y es **tracción-aware** (un tracción-delantera recibe ayuda extra para trepar en la subida). Sin tocar ningún parámetro clava el punto final **por debajo de 0.5 m** en la mayoría de los vehículos, y hasta **~1 m** en los más grandes y de wheelbase largo. Validado out-of-sample en un rango amplio de tracciones y tamaños (Toyota 86 RWD **0.04** · Sedan **0.18** · Golf FWD **0.20** · Hatchback FWD **0.23** · Porsche GT2RS **0.33** · Sedan en reversa **0.47** · **camión Truck_01 0.54** · Offroad 0.09–1.15 m), con **crosstrack ~0.3 m** y rumbo al parar **<1°** en autos (~3° en el camión). Incluye el **endpoint tras una curva** —histórico punto débil— hoy resuelto de raíz: el control **sigue tu velocidad grabada** en la aproximación y sólo frena en los últimos ~3 m (SEQ1 a 44 km/h → **0.34 m**). Generaliza leyendo el juego, no una fórmula.
- **Lo que NO hace:** no inventa rutas (necesita tu grabación o el grafo) ni descubre comportamiento más allá de lo que demostraste. Es determinista e inspeccionable — esa es su gracia.

> En una frase: **fiel y predecible en el régimen normal; el límite honesto está en lo extremo.**

---

## 19. Frontera abierta — llevalo más lejos

Esto es open source por una razón: **llegué hasta acá; el siguiente nivel es tuyo.** Acá está, sin vueltas, dónde está el borde y por dónde se empuja. Si vas a contribuir, arrancá por una de estas.

### El salto grande: manejo extremo (un "v3")
*Dónde está hoy:* maneja al régimen **nominal** del vehículo (§18). Drifts, sobreviraje fuerte, contravolante — el receptor del motor no los reproduce con fidelidad.
*El salto:* override de física propio (saltear el modelo de eAI en esos tramos) o una capa de ML que capture lo que el control clásico no. Es el más grande y el más difícil — y el que abre el manejo "de película".

### Wizard 100% autónomo
*Dónde está hoy:* detecta problemas y propone, pero **tu ojo** confirma (el ciclo A/V/R/I).
*El salto:* que internalice el diagnóstico (bias→`CenterOffset`, saturación→`SteeringScale`, lugging→gear) y **calibre solo**, de la grabación a la ruta lista sin intervención. El esqueleto ya está (la base de heurísticas); falta cerrar el lazo.

### Coordinación multi-vehículo
*Dónde está hoy:* un vehículo por ruta.
*El salto:* convoyes reales — spacing dinámico, el de atrás sigue al de adelante, reformación si uno cae. Hay técnica prestada de ARMA/RV (breadcrumb + separación de convoy) para inspirarse.

### El NPC vivo
*Dónde está hoy:* el chofer maneja, pero es mudo.
*El salto:* un conductor con LLM que hable, reaccione y hasta dé una misión en el viaje. El diseño ya es provider-agnostic (cambiar de modelo = una línea de config).

### Más mundos
- **Trenes** — el caso más fácil: la red *son* los rieles, sin volante ni giros inventados. ~70% cubierto sin tocar nada.
- **Otros motores** — el principio (demostración + lectura de config + control clásico) no es exclusivo de Enfusion; es portable.

> **Si tomás una:** el AI knowledge pack te deja el contexto completo —por qué cada decisión, qué se probó, qué falló— para que arranques **en el borde y no repitas el camino**. Esa es la idea de abrirlo: que el próximo empiece donde yo terminé. Tomala y seguí. 🚀

---

## Apéndice A — Referencia completa de la config de ruta

Todos los campos del JSON (`BZBusRoute*.json`). Casi todos tienen un default sensato: **solo tocás lo que necesitás**. Convención: un valor `-1` o `0` suele significar *"usá el default del código / desactivado"*.

### A.1 Básicos
| Campo | Default | Qué hace |
|---|---|---|
| `VehicleClass` | `"ExpansionBus"` | qué vehículo spawnea |
| `DriverClass` | `eAI_SurvivorM_Boris` | el NPC chofer |
| `RespawnDelay` | `300` | seg antes de respawnear (servicio continuo) |
| `AverageSpeedMS` | `11` | velocidad promedio, para el cálculo de ETA |
| `SpawnHoldSeconds` | `3` | espera antes de arrancar (`0`=al toque, `30+`=esperar trigger de misión) |
| `VehicleInvincible` | `true` | irrompible; `false` = destructible (habilita daño/emboscada) |
| `DriverInvincible` | `true` | Boris irrompible; `false` = mortal (puede morir en combate / al recibir daño) |
| `DriverClothing` | `[]` | ropa del **chofer** (classnames a equipar). Vacío = outfit default del framework. Ver §A.1b |
| `MaxGear` | `6` | marcha máxima de la caja automática (FIRST=2 … SIXTH=7) |
| `Attachments` | `[]` | partes a equipar al spawnear (ruedas, batería, bujías…) |
| `Wheelbase` | `0` | distancia entre ejes (`0`=del código); lo usa el reverse |

### A.1b Cómo vestir al chofer

El framework viste a **Boris (el chofer, seat 0)** con un outfit configurable **por ruta**. Lo definís con `DriverClothing` en el header/JSON de la ruta: una lista de **classnames** de ropa, cada uno se equipa en su slot.

```json
"DriverClothing": ["BZ_AutoDrive_TShirt", "PolicePants", "PoliceCap", "CombatBoots_Black"]
```

- **Si NO seteás `DriverClothing`** (o lo dejás `[]`): Boris usa el **outfit default del framework** — `BZ_AutoDrive_TShirt` (la remera branded, va plegada dentro de `@BZ_AutoDrive`, siempre cargada) + `PolicePants` + `PoliceCap` + `CombatBoots_Black`. Las rutas viejas siguen viéndose igual que siempre.
- **Si seteás `DriverClothing` con items:** esa lista **reemplaza** el default por completo (ponés el outfit entero que querés, no se mezcla).
- Cada classname se equipa con `CreateAttachment` por string: si un classname **no existe o su mod no está cargado**, ese ítem **falla en silencio** (no rompe nada, simplemente no aparece). Usá classnames válidos (de un mod de ropa cargado, o vanilla).

> **Alcance:** esto viste **solo al chofer**. La ropa/loadout de los **bots de convoy/crew** la maneja **Quest (Expansion Quests)**, no el framework — ver §7.8.

### A.2 Velocidad / control de manejo *(avanzado)*

> **El wizard produce el control por default (§9) — normalmente NO tocás estos campos**; quedan acá como **ajuste avanzado**. Por default la ruta convertida usa **tu velocidad grabada** (capeada por la curva) con **modelo inverso**: `FollowPath=false`, `UseInverseModel=true`. Si un modder quiere que Boris **calcule la velocidad por curvatura** en vez de usar la grabada, prende `FollowPath=true`. *(Estos flags eran los viejos "modos 1/2/3", hoy unificados en el control por default — §9.)*

| Campo | Default | Qué hace |
|---|---|---|
| `GearStrategy` | `"auto_box"` | `auto_box` (la AT decide) o `follow_recording` (usa el gear grabado — para sport cars que patinan en 1ra) |
| `FollowPath` | `false` | `true` = calcula la velocidad por **curvatura** (ignora la grabada); `false` (default) = usa tu velocidad grabada |
| `FollowPathLatAccel` | `4.0` | grip lateral (m/s²) → define la vmax por curva |
| `FollowPathMaxKmh` | `50` | tope de velocidad en recta (cuando `FollowPath=true`) |
| `FollowPathCurveSpan` | `5` | espaciado de wps para medir la curvatura (evita ruido) |
| `FollowPathSpeedSmooth` | `8` | suaviza el perfil de velocidad (anticipa curvas) |
| `FollowPathUseReference` | `false` | usa la velocidad grabada capeada por la curva (parte del control por default) |
| `UseInverseModel` | `false` | throttle/brake por PID + modelo inverso (vehicle-agnostic) |
| `InverseModelKp/Ki/Kd` | `-1` | gains del PID de velocidad (`-1`=default) |
| `InverseModelLowRpmMin` | `false` | gear amortiguado (gears altos en cruise, más estable) |
| `TargetSpeedSmoothWindow` | `0` | suaviza la velocidad objetivo (menos oscilación del PID) |
| `AccelShiftThreshold` | `999` | anti-catapulta: sube gear si acelera de más (`999`=off) |

### A.2b Bocina y luces *(replay del humano — §7, §9)*
| Campo | Default | Qué hace |
|---|---|---|
| `HornMode` | `"replay"` | `replay` (toca la bocina donde la grabaste) · `stops` (en cada parada) · `finish` (al llegar al final) · `off` |
| `LightsMode` | `"auto"` | **`auto`** (default — prende de noche **al arrancar el motor**, 19:00–06:00; ignora lo grabado) · **`off`** (siempre apagadas → **desactiva las luces**, p/ misiones nocturnas sigilosas) · `auto_inverted` (apaga de noche → sigilo) · `replay` (prende/apaga donde lo grabaste) · `on` (siempre) |

> **Default `auto`:** todas las tomas prenden los faros solas de noche, sin grabar nada. Las luces se encienden **apenas arranca el motor** (durante el spawn-hold), no a mitad de ruta. Umbral de noche = 19:00–06:00 (hora del mundo del juego).
> **Desactivar las luces:** poné `"LightsMode": "off"` en el header de la ruta (misión sigilosa nocturna → el vehículo no se delata).
> Para que las luces se vean, el vehículo necesita **batería + faros (bombillas) instalados** (§7); el framework energiza la batería al spawn, pero los faros tienen que estar en los `Attachments`.

### A.3 Dirección (steering)
| Campo | Default | Qué hace |
|---|---|---|
| `SteeringScale` | `-1` (auto) | escala del volante; wheelbase corto → bajar (auto lo deriva del wheelbase) |
| `CurvatureSteerBoost` | `0` | amplifica el volante en curva (contra el sub-giro en 90°) |
| `PathSmoothWindow` | `5` | suaviza las posiciones del path |
| `CruiseLateralDeadband` | `0` | "paredón": banda donde no corrige (anti-microvolante); `0.5` recomendado |
| `CruiseLateralKGain` | `1.0` | ganancia de la corrección lateral |
| `CruiseLateralDamp` | `0` | damping del lateral (mata el zigzag); `0.3` moderado |
| `CruiseLateralCenterOffset` | `0` | corrige el sesgo lateral (`+`=derecha, `−`=izquierda) |
| `CruiseHybridSteerThreshold` | `-1` | usa el volante grabado si supera el umbral (pulsos de teclado) |
| `CruiseHybridThrottleThreshold` | `-1` | usa el throttle grabado cuando el humano aceleraba |
| `CruiseFFWeight` | `-1` | peso del feedforward de curvatura en cruise (`-1`=0.25) |
| `CurveThrottleEnabled` (+ `...LookaheadM/StartDeg/FullDeg/MinScale`) | `true` | corta el throttle ANTES de una curva cerrada |

### A.4 Pendientes
| Campo | Default | Qué hace |
|---|---|---|
| `SlopeCompensationEnabled` | `true` | suma throttle en subida, resta en bajada |
| `SlopeLookaheadWps` | `5` | cuántos wps adelante mira la pendiente |
| `SlopeGain` | `1.0` | cuánto compensa (`1`=completo) |
| `SlopeLateralGain` | `1.0` | corrige el bias lateral que mete la pendiente |

### A.5 AutoRecovery (que siempre llegue)
| Campo | Default | Qué hace |
|---|---|---|
| `AutoRecoveryEnabled` | `false` | si Boris se traba, lo teleporta adelante |
| `AutoRecoveryStuckTimeS` | `10` | seg de trabado antes de teleportar |
| `AutoRecoveryAdvanceWps` | `5` | cuántos wps adelante teleporta |
| `AutoRecoveryCooldownS` | `8` | mínimo entre teleports (anti-spam) |
| `AutoRecoveryMaxPerMission` | `0` | `0`=ilimitado, `X`=falla la misión si supera |
| `StuckAdvanceTimeoutS` | `0` | safety **separada** del AutoRecovery: si no avanza ningún wp por N s, **empuja** el índice (no teleporta). `0`=default 60 s. Corre aunque AutoRecovery esté off; subir mucho para deshabilitar |

> **Filosofía "no salvar a Boris de Boris":** el AutoRecovery viene **OFF** a propósito. Sobre terreno limpio Boris es preciso; si se traba ahí, es un problema de **grabación** (re-grabá el tramo), no algo que el teleport deba tapar. Prendelo solo si querés la garantía absoluta de "siempre llega" (servicio desatendido) asumiendo el salto visual.

### A.5b Obstáculos externos — AR_OnWay *(validado en 5 vehículos)*
Distinto del AutoRecovery (que cuida a Boris de trabarse **solo**), esto lo cuida del **mundo**: otro vehículo detenido en el camino, o uno que lo choca/empuja. Dos flags independientes; **el wizard los setea** al convertir (te pregunta el perfil).
| Campo | Default | Qué hace |
|---|---|---|
| `ObstacleSlow` | `false` | freno **predictivo** ante un vehículo adelante (lookahead que crece con la velocidad y la física del vehículo; frena a su máximo real hasta detenerse antes del obstáculo) |
| `ObstacleEscape` | `false` | si el obstáculo **persiste** (o lo chocan/empujan fuera de la línea), **teleporta** al primer wp limpio pasado el obstáculo |
| `ObstacleScanDist` | `50` | distancia mínima (**piso**) de escaneo del path; el lookahead real crece con la velocidad |
| `ObstacleStopDist` | `15` | a cuántos metros del obstáculo se detiene |
| `ObstacleCorridorHalf` | `2.3` | medio-ancho del carril: un auto en la **banquina** (offset lateral > esto) NO frena a Boris |
| `ObstacleEscapeWaitS` | `6` | seg frenado ante el obstáculo antes de escapar |
| `ObstacleEscapeResumeKmh` | `10` | velocidad suave al retomar tras el teleport |

> **Perfiles (el import v1 los pregunta; en una toma nueva se setean por config):** **Transporte robusto** = ambos ON (el bus sortea lo que le tapa el camino). **Interceptable** = `ObstacleSlow` ON + `ObstacleEscape` **OFF** (Boris frena lindo ante el que lo bloquea pero **no** se escapa → la misión de interceptación funciona). **Ninguno** = ambos OFF (réplica pura). Requiere `UseInverseModel=true` (el control por default).

### A.6 Convoy / Quest *(ver §12)*
| Campo | Default | Qué hace |
|---|---|---|
| `ConvoyMode` | `""` | `""` / `"flee_on_kill"` / `"ambush_on_damage"` |
| `Crew` | `[]` | bots que viajan desde el arranque (§12.7) |
| `Events` | `[]` | nodos de evento (§10, §12.7) |

### A.7 Reversa e intercambios — *avanzado*
*Casi no se tocan: el flujo actual es automático. Están acá por si afinás una maniobra fina.*

> **El flujo hoy es simple.** La **reversa** se auto-detecta del **gear grabado**, la **aproximación** es automática (`ApproachAuto`) y el **cambio de sentido** se **auto-detecta** del cambio de gear forward↔reverse —sin tecla— (§9.4). Los viejos **modos de waypoint marcados a mano** (`parking`, `maniobra`, `approach`) **ya no se producen** — sus campos quedan solo por compatibilidad con tomas viejas (marcados *legacy* abajo).
| Campo | Default | Qué hace |
|---|---|---|
| `DirectReplayFromWaypoint` | `-1` | desde este wp, replay literal de los inputs grabados |
| `ManiobraTargetSpeedCap` | `18` | *(legacy)* tope del viejo modo `maniobra` (deprecado, no se produce) |
| `ParkingTargetSpeedCap` | `0` | *(legacy)* tope del viejo modo `parking` (deprecado, no se produce) |
| `ModeEntrySnapEnabled` / `...MaxDist` | `false` / `0.5` | snap (teleport) a la pos+heading grabados al entrar a la **reversa**. **OFF por default**: el control genuino posiciona sin teleport (§9.5); reactivalo solo si una ruta puntual lo necesita |
| `AntiRollbackEnabled` / `...PitchThreshold` | `true` / `0.05` | clava frenos en pendiente a velocidad ~0 (no rueda hacia atrás) |
| `ParkingStanleyK` / `ParkingFFWeight` | `-1` | *(legacy)* Stanley del viejo modo `parking` (deprecado) |
| `ReverseStanleyK` / `ReverseFFWeight` / `ReverseFFSign` / `ReverseFFMaxSteerRad` | `-1`/`-1`/`0`/`0` | control del modelo-bicicleta en reversa |
| `ReverseSteerGateOffset` / `ReverseSteerThrottleFloor` / `ReverseSteerMax` | `0` | gates anti-stall y anti-overshoot en reversa |
| `ReverseRecordedSteerThreshold` | `0` (→0.2) | seguir el volante grabado en reversa (recording-as-manual) |
| `ReverseTargetSpeedCap` / `ReverseStanleyFineMax` / `ReverseHeadingDeadbandDeg` | `0` | techo de velocidad / corrección fina / deadband de ángulo en reversa |
| `EndFreezeDisabled` | `0` | `0`=al final, frena y congela donde el humano se detuvo; `1`=sigue sin frenar |
| `ReverseStanleyMinSpeed` | `0` (→2 m/s) | piso de velocidad para la corrección lateral en reversa (rompe la espiral 1/v a baja velocidad) |
| `ApproachAuto` | `false` | freno predictivo **automático** a la maniobra — es el comportamiento por default (ya no se marca `approach` a mano) |
| `ApproachExitKmh` | `0` (→~20) | velocidad objetivo al final de la rampa de aproximación |

> **Velocidad de reversa — automática, sin config (validado en 4 vehículos, R_min 3.44–4.57).** Boris reversa a `min(velocidad grabada, velocidad que su física le permite para el arco)` → generaliza a cualquier vehículo (uno de giro ancho reversa más lento y por eso sigue mejor el arco). Al acercarse al **final** del tramo de reversa frena solo (*endpoint-taper*): en **plano** llega a paso de hombre (mata la sobre-pasada de la salida), en **cuesta arriba** mantiene el momentum para trepar (no frena de más). No hay campos que tocar — sale de la física del vehículo y de la pendiente.

> **Corte de la maniobra en recta (para generalizar — §9.5).** El replay directo (`DirectReplayFromWaypoint`) reproduce **ángulos de volante específicos del vehículo**: si el tramo de reversa empieza **en una curva**, un vehículo con otro wheelbase se abre. Hacé el **cambio de sentido** (frená del todo antes de invertir la marcha, ahí el auto-detect corta el tramo) **donde la trayectoria ya es recta** (la curva la resuelve el control cerrado, agnóstico al vehículo). Ojo con el auto-approach (`ApproachAuto`): puede **frenar de más** si el corte cae en una recta rápida — por eso conviene cortar donde ya venís lento.

> **Intercambio reversa→forward + endpoint autoadaptativo (RESUELTO).** El *cusp* (reversa→forward, el K-turn adentro de un galpón) quedó **validado en un banco amplio de tracciones y tamaños (FWD / RWD / motor-central / 4x4 / camión), con 0 rescates de AutoRecovery** — con reglas físicas (CuspExitHeadingBand + supresión-de-parada con latch anti-"pasitos"), sin constantes por-vehículo. Y el freno de parada del **endpoint final se autoconfigura por vehículo + piso**: `GetMaxBrakeDecel` (torque de freno del config / radio / masa) acotado por el **agarre real de la superficie** bajo Boris, **tracción-aware** → clava el punto **por debajo de 0.5 m** en la mayoría (hasta ~1 m en los más grandes y de wheelbase largo) sin tocar nada. Validado out-of-sample (Toyota 86 RWD **0.04** · Sedan **0.18** · Golf FWD **0.20** · Hatchback FWD **0.23** · Porsche GT2RS motor-central **0.33** · Sedan en reversa **0.47** · camión Truck_01 **0.54** · Offroad 0.09–1.15 m), con crosstrack **~0.3 m** y rumbo al parar **<1°** en autos (~3° en el camión). Incluye el **endpoint tras una curva** (histórico punto débil): el control sigue la velocidad grabada en la aproximación y sólo frena en los últimos ~3 m (SEQ1 a 44 km/h → **0.34 m**).
| Campo | Default | Qué hace |
|---|---|---|
| `EndpointThrottleCapEnabled` | `true` | activa el corte de gas + freno autoadaptativo en la zona del endpoint final |
| `EndpointStopDecelMS` | `0` (→auto) | decel objetivo de la parada; `0` = la deriva del config del vehículo × agarre del piso real |
| `EndpointStopDecelFactor` | `0.85` | fracción del freno máximo real que aplica (margen para no bloquear) |
| `FwdClimbFactor` | `1.6` | ayuda de throttle sostenida a un **tracción-delantera** para trepar en la subida del endpoint (evita los "pasitos") |

### A.8 Waypoints
Cada entrada de `Waypoints[]`:
| Campo | Qué es |
|---|---|
| `pos` | `[x, y, z]` la posición |
| `targetSpeed` | velocidad objetivo (km/h) en ese punto |
| `targetGear` | marcha (si `follow_recording`) |
| `mode` | `normal` o `reverse` — la reversa se **auto-detecta del gear** grabado; el cambio de sentido va aparte en `legBreak`. *(Los viejos `parking`/`maniobra`/`approach` ya no se producen.)* |
| `isStop` / `stopDuration` / `stopRadius` | parada: si es stop, cuánto frena, radio |
| `targetThrottle/Brake/Steering` | inputs grabados (solo se usan si `hasInputData=1`, replay literal — avanzado) |
| `hasInputData` | `1` = replay literal de tus inputs (avanzado, mismo vehículo); `0` (default) = control por config |
| `name` | etiqueta del punto (las paradas) |

---

## Apéndice B — Para desarrolladores: extender con código

Si querés ir más allá del JSON —sumar un verbo, un trigger o un comportamiento— acá están los puntos de extensión. Es **Enforce** (el scripting de DayZ).

### B.1 El corazón: cómo el framework maneja el vehículo
El framework inyecta los inputs en el `OnInput` de un `modded class CarScript`, **después** del de eAI (override-last):
```c
modded class CarScript {
    override void OnInput(float dt) {
        super.OnInput(dt);                    // eAI corre su lógica (incluido ShiftTo(FIRST))
        if (!GetGame().IsServer()) return;
        BZBusService srv = BZBusService.GetInstance();
        if (!srv || !srv.IsBusActive(this)) return;
        // ... acá el framework sobreescribe gear/throttle/steer con SU control ...
    }
}
```
> **El breakthrough fue ese orden:** eAI fuerza 1ra cada frame; nuestro override corre DESPUÉS y pone el gear/inputs correctos.

Superficie que el framework **escribe** sobre `Car`: `SetThrottle(0..1)`, `SetSteering(-1..1)`, `SetBrake`, `SetHandbrake`, `ShiftTo(gear)`, `EngineStart/Stop`. Y **lee**: `GetSpeedometer()`, `EngineGetRPM()`, `WheelGetContactPosition(i)` (de ahí sale el wheelbase), `WheelGetSurface(i)`, etc.

### B.2 Agregar un verbo nuevo
Todos los verbos viven en `BZBusService.ExecuteAction(Car car, BZAction action, int evIdx)`, un `else if` por verbo. Patrón real del código:
```c
private void ExecuteAction(Car car, BZAction action, int evIdx) {
    if (!action) return;
    string verb = action.verb;
    if (verb == "start_engine") {
        if (!car.EngineIsOn()) car.EngineStart();
    } else if (verb == "freeze_vehicle") {
        m_Frozen = true;
    }
    // ... sumás tu rama:
    } else if (verb == "mi_verbo") {
        // tenés a mano: car (el vehículo), action (sus campos: value, fvalue, msg, slot...),
        // m_WaypointIndex (dónde está). Hacé lo tuyo:
        car.SetHandbrake(1.0);
        BZBusLog.Info("[EVENT " + evIdx + "] mi_verbo ejecutado.");
    }
}
```
El `delay` (coreografía) ya lo maneja el dispatcher por vos (`CallLater`). Y listo: el modder ya puede poner `{ "verb": "mi_verbo" }` en sus `Events[]`. *(Para campos nuevos, agregalos a la clase `BZAction` — es plana, no rompe el parser.)*

### B.3 Agregar un trigger nuevo
Los triggers se evalúan por `BZTrigger.type`. Sumás el campo que necesite a la clase `BZTrigger` y un `case` en la evaluación (ej. `on_player_shoot` con su condición).

### B.4 El hook de Quest
La integración con Expansion-Quests entra por un `modded class MissionServer`:
```c
modded class MissionServer {
    override void Expansion_OnQuestStart(ExpansionQuest quest) {
        super.Expansion_OnQuestStart(quest);
        BZBusService.GetInstance().OnQuestStart(quest);   // el framework "escucha" el quest
    }
}
```
Desde `OnQuestStart`, el framework **pollea** `ExpansionQuestModule…QuestPatrolExists(questID, patrols)` para alcanzar los bots vivos (son lazy por proximidad). *(La subclase del objetivo `ExpansionQuestObjective*Event` NO compila en nuestro scope → por eso se usa el hook de MissionServer + poll.)*

### B.5 Gotchas de Enforce (los que nos pegaron)
Enforce no es C# ni C++; estas trampas costaron horas:
- **No hay ternario** `?:` ni `if` multilínea con `&&` al inicio de línea → "Syntax error". Usá `if/else` o bools intermedios.
- **No existen** `Math.PI` ni `Math.AbsFloat`.
- **`new Clase(args)` revienta** → creá vacío y seteá los campos.
- **No moddees clases del engine** (`CGame`, etc.) → rompe al cargar.
- **"Formula too complex"** a ~9 operandos con `+` → dividí en varias líneas con `+=`.
- **Scope de ramas hermanas:** variables en distintas ramas de un `if/else` (o `case`) **chocan** (no tienen scope propio como en C++) → renombralas o hoisteá.
- **Aritmética inline en concat de string** (`"x" + (seat-1)`) rompe → hoisteá a una `int` antes.
- **AddonBuilder NO valida Enforce** — compila igual con errores de sintaxis; recién fallan al **cargar el server**. Confirmá siempre en el RPT.

> Estos gotchas (y más) van también al **AI knowledge pack**, para que una IA te avise antes de que los pises.

### B.6 — Integrar BZ_AutoDrive como dependencia de tu mod
Como es open source, podés construir **tu propio mod sobre** BZ_AutoDrive: que tu mod de misiones, eventos o transporte use el framework para el **manejo**, sin reimplementar nada. Hay dos formas de acoplarte:

**(a) Como dependencia + llamando su API.** En el `config.cpp` de tu mod, dependé del addon:
```cpp
class CfgPatches {
    class TuMod {
        requiredAddons[] = { "BZ_AutoDrive" };   // el addon del framework
    };
};
```
Y desde tu código, el servicio es un singleton — le pedís que spawnee/maneje una ruta tuya:
```c
BZBusService srv = BZBusService.GetInstance();
srv.RespawnFromPath("BZBusRoute_MiRuta.json");   // spawnea el vehículo y maneja esa ruta
// (RespawnAs(clase) para cambiar el vehículo en runtime)
```
Tu mod decide **cuándo** (un trigger tuyo, un horario, un evento de tu mod); el framework pone el **manejo**.

**(b) Extendiéndolo con `modded class`** (B.1–B.4): sumás verbos, triggers o hooks propios sin tocar el código del framework.

> *Ejemplo:* tu mod de misiones, al iniciar una, llama a `RespawnFromPath` para que un convoy salga manejando — vos ponés la lógica de misión, el framework el vehículo. (Misma división que con Quest, pero desde **tu** mod.)

> *Naming:* el addon se llama **`BZ_AutoDrive`**; el mod-demo del bus (el servicio costero) es contenido aparte que lo usa. Es el nombre que va en `requiredAddons`.


### B.7 — Build & deploy your fork

**Para solo usar el mod:** suscribite en el Workshop — ya viene firmado, no tenés que compilar nada.

**Para compilar tu fork:** empaquetá el PBO como prefieras —**DayZ Tools** (AddonBuilder) o tu propio pipeline— y firmalo con tu **propia** llave (`.biprivatekey`). No se distribuye ningún script de build: cada modder empaqueta a su manera. Copiá tu `.bikey` a la carpeta `keys\` del server; sino el cliente patea con "faltan PBO".

**El único gotcha que conviene recordar:** cuando renombrás, movés o borrás un `.c`, **limpiá la carpeta `temp/` de AddonBuilder antes de reempaquetar** — sino empaqueta versiones viejas ("zombie") del código. Después de eso, validá RPT sin errores + smoke test ingame antes de subir al Workshop.
