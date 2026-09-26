# ============================================================================
#  frame_to_route.py - Conversor GRABACION -> RUTA (brick #1 del import del editor)
#
#  TESIS (project_generative_matches_human / project_editor_merge_design):
#  una grabacion y un dibujo son LO MISMO -> una LINEA + un PERFIL DE VELOCIDAD.
#  La grabacion entra al MISMO pipeline que el dibujo: pure-pursuit sigue la linea
#  + velocidad honrada (FollowPaintedToStop + modo-2) + gears automaticos
#  (InverseModel). Se DESCARTAN steering/throttle/gear grabados (hasInputData=0):
#  Boris los DERIVA. NO es frame-replay.
#
#  Convierte frame_<ts>_<veh>.csv (40Hz: x,y,z,speed_kmh,gear,heading,is_marker)
#  a los archivos de ruta que el server lee (BZBusRoute_<name>.json + _hdr.json +
#  _wp.csv). Densidad NATIVA (sin smoothing/downsampling; la forma fina de la linea
#  ES el volante del humano). Reversa auto-detectada (gear==0 o movimiento hacia
#  atras vs heading). Eventos desde is_marker (NUMPAD 4 durante la grabacion).
#
#  Uso:
#    python frame_to_route.py <frame_csv> <route_name> [--profile DIR] [--min-move M]
#  Ej:
#    python frame_to_route.py "C:\...\frame_230713-021123_Sedan_02_Grey.csv" FRAME01
#
#  Esquema wp.csv (BZBusService.LoadWaypointsCSV, min 14 cols, SplitKeepEmpty):
#    0-2 pos | 3 isStop | 4 stopDur | 5 stopRadius | 6 targetSpeed | 7 gear
#    8-11 thr/brk/hb/steer | 12 hasInputData | 13 mode | 14 name
#    15 heading | 16 lights | 17 horn | 18 frontWheel | 19 corridorHalfWidth
# ============================================================================
import csv, os, math, json, sys, argparse

# ⚠⚠ LA CONSOLA DEL WIZARD ES cp1252. route_wizard.ps1 corre este script con
# `2>&1 | Out-String`: con la salida en un PIPE, Python escribe en cp1252, y un simbolo que no
# existe ahi (el aviso de traia uno) tiraba UnicodeEncodeError -> exit 1 -> el wizard
# decia "error de conversion" DESPUES de haber escrito la ruta. Misma cicatriz que
# envelope_desde_toma.py: "un instrumento que crashea al reportar no es un instrumento".
# ✖ NO se cambia la codificacion a utf-8 (lo que hace envelope_desde_toma): las lineas de siempre
#   ya imprimen `·` en cp1252 y saldrian con otros bytes. Solo `errors="replace"`: lo que no entra
#   sale como `?` en vez de matar el proceso. Los avisos nuevos son ASCII igual.
try:
    sys.stdout.reconfigure(errors="replace")
    sys.stderr.reconfigure(errors="replace")
except AttributeError:
    pass

STOP_KMH   = 1.0     # <= esto en el ultimo punto -> isStop=1 (clava el freno en el endpoint)
REV_DOT    = -0.30   # dot(forward, movimiento) por debajo de esto -> reversa (backup a baja vel)
REV_MAXKMH = 15.0    # el motion-dot SOLO cuenta por debajo de esto (reversa real = maniobra lenta)
REV_MINMOVE= 0.15    # y con desplazamiento real (evita ruido/glitches de 2 frames casi iguales)

# RESAMPLEO A RESOLUCION FISICA (2026-07-13): la grabacion a 40Hz tiene el auto a 0.1-0.2m
# entre frames a baja/media velocidad. El micro-jitter de la fisica (suspension, scrub) sobre
# esos baselines minusculos se lee como curvas de 100-1500 grados/5m -> el detector de curvatura
# acorta el Ld -> a 40-60 km/h el avance-por-tick supera el Ld -> ZIGZAG. El volante REAL del
# humano vive a escala de METROS; el wiggle sub-metro es ruido que ningun auto puede seguir.
# Muestreamos la linea cada SPACING (adaptativo por velocidad): fino en maniobra lenta (detalle
# real + el auto puede seguirlo), grueso en recta rapida (mata el ruido, modo-2 promedia).
SPACING_K   = 0.15   # spacing objetivo = v_ms * K  (~0.15s de recorrido)
SPACING_MIN = 0.50   # piso: preserva el detalle de maniobras lentas (galpon)
SPACING_MAX = 2.00   # techo: en recta rapida no hace falta mas denso

ENDPOINT_DECEL = 1.40  # m/s^2 de la rampa de aproximacion al stop. SUBIDO 0.60->1.40 (2026-08-11, ENDPOINT TRAS
                       # CURVA): a 0.60 la rampa era 2.5x mas conservadora que el freno REAL de Boris (~1.4-1.5,
                       # medido por el iman EndpointBrakeDecel) -> obligaba a Boris a 3.7 km/h a 1m mientras el humano
                       # cargaba ~11 planos y clavaba (carry+slam) -> REPTABA 15s vs 2.75s del humano (endpoint pegado
                       # a la salida de una curva). A 1.40 la rampa queda POR ENCIMA de la velocidad grabada en toda
                       # la salida de curva -> el min(rampa,grabado) DEFIERE al envion del humano y solo limita los
                       # ultimos ~2m; el iman clava la parada. Fisicamente consistente: rampa(1.4) ~= freno del iman
                       # (1.5) -> Boris carga exactamente lo que despues puede frenar (cero muleta). Cubre tambien los
                       # legBreak/intercambios (mismo loop) -> levanta la aproximacion de REVERSA. Ver [[project_endpoint_after_curve]] [[project_endpoint_carry_slam]].
ENDPOINT_RAMP_M = 25.0 # hasta cuantos metros antes del stop aplicar la rampa (mas alla domina lo grabado)
STOP_HOLD_S    = 6     # seg detenido en el endpoint: le da TIEMPO al creep fino de AtStop de reptar al
                       # punto EXACTO. Con 0 la ruta completa al instante (handbrake+despawn) y el creep
                       # no converge -> Boris queda ~2m corto. El creep es lento (~0.32 m/s): con 3s
                       # llego a 1.10m todavia convergiendo -> 6s para cerrar los ~2m. un jugador: "detenido 2 seg".

def resample(rows):
    """Muestrea la linea a arc-length adaptativo por velocidad. Conserva primero, ultimo y
    los frames con marca. Devuelve la sublista de rows elegidos (posiciones NATIVAS, no promediadas)."""
    out = [rows[0]]
    acc = 0.0
    prev = rows[0]
    for q in rows[1:]:
        step = math.hypot(q["x"] - prev["x"], q["z"] - prev["z"])
        prev = q
        # DETENIDO NO ACUMULA NI EMITE (2026-07-22, un jugador). Quedarse quieto para un cruce, armar una
        # maniobra o sincronizar una escena NO tiene que generar waypoints. El jitter de suspension
        # (±0.02-0.05m/frame a 40Hz) suma longitud de arco aunque el desplazamiento NETO sea ~0 ->
        # emitia un puñado de wps casi coincidentes en el mismo punto -> el pure-pursuit apunta a un
        # aim que salta y el leg logic se confunde (ensucia el control). Solo se acumula/emite EN
        # MOVIMIENTO; las paradas de verdad se DECLARAN (marca) y se conservan en preservar_paradas.
        parado = q["spd"] < STOP_KMH_DET
        if not parado:
            acc += step
        v_ms = q["spd"] / 3.6
        target = min(SPACING_MAX, max(SPACING_MIN, v_ms * SPACING_K))
        if q["mark"] != 0:                    # marca (1 = evento, 2 = intercambio): SIEMPRE se conserva
            out.append(q); acc = 0.0
        elif not parado and acc >= target:    # emite solo moviendose (detenido colapsa a nada)
            out.append(q); acc = 0.0
    if out[-1] is not rows[-1]:
        out.append(rows[-1])
    return preservar_paradas(rows, out)

STOP_KMH_DET = 0.6    # por debajo de esto se considera DETENIDO
STOP_MIN_S   = 0.4    # y si dura mas que esto, es una PARADA con intencion

def preservar_paradas(rows, out):
    """Una parada se DECLARA (marca de intercambio/evento) o se infiere del endpoint; NO de un 0 km/h
    suelto. Una parada INCIDENTAL (cruce, maniobra, sync de escena, SIN marca) se cruza sin parar ->
    no toca la velocidad, no deja waypoint. Solo los bloques detenidos que CONTIENEN una marca fuerzan
    el 0 en su waypoint, para que Boris baje del umbral de reversa y encuentre el intercambio (medido
    2026-07-20: sin esto la ruta quedaba diciendo 4.1 km/h en la transicion y Boris no metia reversa).
    El endpoint se maneja aparte en main() (isStop)."""
    if not rows or not out:
        return out
    bloques = []
    ini = None
    for i, r in enumerate(rows):
        parado = r["spd"] < STOP_KMH_DET
        if parado and ini is None:
            ini = i
        elif not parado and ini is not None:
            if rows[i-1]["t"] - rows[ini]["t"] >= STOP_MIN_S:
                bloques.append((ini, i-1))
            ini = None
    if ini is not None and rows[-1]["t"] - rows[ini]["t"] >= STOP_MIN_S:
        bloques.append((ini, len(rows)-1))
    ndecl = 0; nincid = 0
    for a, b in bloques:
        # SOLO las paradas DECLARADAS (marca dentro del bloque) fuerzan el 0. Sin marca = incidental,
        # se cruza sin parar (un jugador 2026-07-22: "me gustaria quedarme detenido en la grabacion, sea por
        # un cruce, por maniobra o para sincronizar una escena" -> sin que afecte el control).
        declarada = False
        for k in range(a, b + 1):
            if rows[k]["mark"] != 0:
                declarada = True; break
        if not declarada:
            nincid += 1
            continue
        cx = rows[(a+b)//2]["x"]; cz = rows[(a+b)//2]["z"]
        mejor = None; mejord = 1e18
        for w in out:
            d = (w["x"]-cx)**2 + (w["z"]-cz)**2
            if d < mejord:
                mejord = d; mejor = w
        if mejor is not None:
            mejor["spd"] = 0.0
            mejor["es_parada"] = True
            ndecl += 1
    if bloques:
        print("  paradas: %d declaradas (0 km/h) | %d incidentales (se cruzan sin parar)" % (ndecl, nincid))
    return out

def load_header(frame_path):
    """Lee el header_<base>.txt apareado (mismo dir). Devuelve dict del fingerprint."""
    hdr_path = frame_path.replace("frame_", "header_").rsplit(".", 1)[0] + ".txt"
    if not os.path.isfile(hdr_path):
        raise SystemExit("[ERROR] no encuentro el header apareado: " + hdr_path)
    hk = {}
    for ln in open(hdr_path, encoding="utf-8"):
        if "=" in ln:
            k, v = ln.strip().split("=", 1); hk[k] = v
    return hk

# ★★★ LA CINTA (E1, ): emision full-res reparametrizada por arco. Lector
# PROPIO de columnas crudas (no toca load_frames: la conversion queda identica).
# La rueda es front_wheel_deg (LOGRADA, continua, 0.18/s) -- NO `steering` (tecla
# binaria, cicatriz col11). chassis_kmh con fallback a speed_kmh (formatos viejos).
# Glitch = salto >CINTA_GLITCH_M en UN frame de 25 ms: fisicamente imposible (a 235
# km/h un frame son 1.63 m) = teleport del logger -> descartado y REPORTADO, para
# que la cinta no lleve arco fantasma.
CINTA_GLITCH_M = 2.0
GLITCH_RACHA_AVISO = 20   # filas rechazadas SEGUIDAS a partir de las cuales se avisa: 0.5 s
# ⚠⚠⚠ REBOBINADO: EL SERVER CORRIGE AL CLIENTE HACIA ATRAS (rubber-band) Y LA TOMA LO GRABA
# No es una meseta que se pone al dia: es el cliente pasado de largo y
# devuelto por el server, con el tramo grabado DOS veces. Medido en 24 cintas:
#     SUZUKI 12 eventos  -2.2 a -14.8 m  lat <= 0.20  desde 171 km/h
#     SPEED01 5          -9.3 a -11.6 m  lat <= 0.20  desde 189 km/h
#     HX58    3          -6.2 a  -6.5 m  lat <= 0.11  desde  78 km/h   <- no es solo alta vel
#     F350    2          -9.2 a  -9.6 m  lat <= 0.03  desde 117 km/h
#     las otras 20: cero.   100% hacia atras, lateral ~0.
# El eje s se arma con la CUERDA entre anclas, que es SIN SIGNO: el salto hacia atras
# entraba como AVANCE y despues el re-recorrido volvia a sumar. Arco fantasma ~2x|salto|
# (SUZUKI ~238 m). Y todos los retrocesos de la SUZUKI superan PERDIDA_M=6.0 del lector:
# el primero cae en s=331 a 171 km/h, EXACTAMENTE donde arranca su fallback.
# ✖ Interpolar x,z en la meseta previa NO sirve (probado offline): parte el salto en dos
# de 7.4 m, y siguen >6. El problema no es resolucion: es GEOMETRIA DUPLICADA.
REBOB_LON_M   = -2.0   # un paso de UN frame de 25 ms asi hacia atras es fisicamente imposible
                       # (a 15 km/h un frame avanza 0.10 m). WRX tiene retrocesos de 0.6-1.1 m
                       # que NO son esto: con -2.0 quedan intactos; si se baja, dejan de estarlo.
REBOB_VMIN_KMH = 15.0  # en maniobra lenta / reversa no hay direccion de marcha confiable,
                       # y la reversa y los cusps son LEGITIMOS: no se tocan.
REBOB_LAT_MAX  = 1.0   # un salto de costado no es un rebobinado (todos los medidos: <= 0.20)
REBOB_VENTANA  = 20    # filas para estimar la direccion de marcha, ANTES del salto
REBOB_TOPE_S   = 3.0   # si no vuelve a pasar el punto en 3 s, no era un rebobinado: no se toca


def descartar_rebobinados(limpias):
    """Saca el tramo DUPLICADO por cada correccion del server hacia atras.

    Por evento: la referencia es el ultimo punto ANTES del salto (el mas lejano que se
    alcanzo). Se descartan las filas posteriores mientras sigan DETRAS de esa referencia
    sobre la direccion de marcha, y se retoma en la primera que la vuelve a pasar. Asi el
    empalme es un paso NORMAL hacia adelante.

 SIN HUECO, a proposito: un hueco reproduce el bug de -- su cuerda recta
    desincronizaba la odometria del lector. Lo que se saca tiene que empalmar.
    ✖ Se descarta el RE-RECORRIDO y no el tramo previo que "se paso": las dos opciones son
    geometricamente equivalentes (el lateral de todos los eventos medidos es <= 0.20 m, o
    sea que es la MISMA linea y solo cambia CUANDO se paso por ella), y esta no reescribe
    filas ya aceptadas.
    """
    import math
    out = []
    n = len(limpias)
    i = 0
    eventos = 0
    abortados = 0
    while i < n:
        fr = limpias[i]
        if len(out) > REBOB_VENTANA and fr["ckmh"] > REBOB_VMIN_KMH:
            a = out[-1 - REBOB_VENTANA]
            b = out[-1]
            dx = b["x"] - a["x"]
            dz = b["z"] - a["z"]
            L = math.hypot(dx, dz)
            if L > 1e-6:
                ux = dx / L
                uz = dz / L
                px = fr["x"] - b["x"]
                pz = fr["z"] - b["z"]
                lon = px * ux + pz * uz
                lat = abs(-px * uz + pz * ux)
                if lon < REBOB_LON_M and lat < REBOB_LAT_MAX:
                    # buscar donde vuelve a pasar la referencia `b`
                    # ⚠ DOS SALIDAS DISTINTAS, y confundirlas dejo un rebobinado sin reparar:
                    #   - SE ACABARON LOS DATOS sin repasar: es un rebobinado DE COLA.
                    # ⛔ RETRACTADO: el "caso medido en el F350" NO era una cola -- la
                    #     toma termina DETENIDA; era la CASCADA del filtro de glitch (ver emitir_cinta).
                    #     La rama queda porque la logica es correcta, pero no hay toma real que la use. Texto viejo:
                    #     el F350: salto de -9.18 m a 0.73 s del final de la grabacion; nunca
                    #     vuelve a pasar el punto porque no hay mas toma. Todo lo que queda
                    #     esta DETRAS de la referencia = geometria duplicada -> se descarta, y
                    #     no deja hueco porque no hay nada despues que empalmar.
                    #   - PASARON REBOB_TOPE_S sin repasar: no era un rebobinado (un giro real,
                    #     una maniobra) -> no se toca.
                    j = i
                    tiempo = 0.0
                    agoto_tiempo = False
                    while j < n:
                        qx = limpias[j]["x"] - b["x"]
                        qz = limpias[j]["z"] - b["z"]
                        if qx * ux + qz * uz >= 0.0:
                            break
                        tiempo += limpias[j]["dt"] if limpias[j]["dt"] > 0 else 0.025
                        if tiempo > REBOB_TOPE_S:
                            agoto_tiempo = True
                            break
                        j += 1
                    if not agoto_tiempo:
                        eventos += 1
                        # una MARCA (evento / intercambio) no se puede perder con el tramo: se muda a la
                        # fila donde empalma (o a la ultima aceptada si es rebobinado de cola). Sin marcas
                        # en el tramo -- el caso de todas las tomas medidas -- no toca nada.
                        marca = 0
                        for k in range(i, j):
                            if limpias[k].get("mark", 0) != 0:
                                marca = limpias[k]["mark"]
                        if marca != 0:
                            destino = limpias[j] if j < n else out[-1]
                            if destino.get("mark", 0) == 0:
                                destino["mark"] = marca
                        i = j          # j == n (cola): corta ahi; j < n: empalma
                        continue
                    abortados += 1
        out.append(fr)
        i += 1
    return out, eventos, abortados


def emitir_cinta(frame_path, out_path):
    filas = []
    with open(frame_path, encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            try:
                filas.append({
                    "x": float(r["x"]), "y": float(r["y"]), "z": float(r["z"]),
                    "hdg": float(r["heading"]),
                    "ckmh": float(r.get("chassis_kmh", 0) or r["speed_kmh"]),
                    "fw": float(r.get("front_wheel_deg", 0) or 0),
                    "thr": float(r.get("throttle", 0) or 0),
                    "brk": float(r.get("brake", 0) or 0),
                    "gear": int(float(r["gear"])),
                    "hb": float(r.get("handbrake", 0) or 0),
                    "dt": float(r.get("dt", 0) or 0),
                    "mark": int(float(r.get("is_marker", 0) or 0)),
                })
            except (ValueError, KeyError):
                pass
    if len(filas) < 2:
        print("[cinta] grabacion vacia: NO se emite")
        return 0
    # ⚠⚠⚠ EL UMBRAL DE GLITCH ES CONSCIENTE DE LA VELOCIDAD. El fijo
    # de 2.0 m TRUNCABA las cintas rapidas en cascada: a 154 km/h las mesetas de
    # cuantizacion del logger hacen pasos de ~2.1 m (2 frames de 1.07 m juntos);
    # el primer paso >2.0 se descartaba, y como la referencia era el ultimo frame
    # RETENIDO (rancia), TODOS los frames siguientes quedaban a >2 m de ella ->
    # cascada. Medido en el m4w: frame crudo 5191 frames dt=0.025 limpios, primer
    # descarte en el frame 779 a 154 km/h = EXACTAMENTE donde terminaba la cinta
    # (4412 frames tirados); las 3 SPEED corrian su tramo rapido en FALLBACK y el
    # m4w clavo un cartel a 159. El umbral honesto: un teleport es un salto
    # IMPOSIBLE PARA LA VELOCIDAD GRABADA — piso 2.0 (guarda a baja v) o 5x lo
    # que la velocidad recorre en el dt del frame (deja pasar mesetas de 2-3
    # pasos; un teleport real de decenas/cientos de metros sigue muriendo).
    # ⚠⚠⚠ Y LA REFERENCIA RANCIA VOLVIO POR OTRA PUERTA. Dos cambios, un
    # solo mecanismo -- el tope se medía contra el ultimo frame RETENIDO como si fuera el ANTERIOR:
    #  (1) EL TOPE CRECE CON EL TIEMPO desde la referencia: v * (transcurrido + 11 dt). Entre filas
    #      consecutivas transcurrido = dt -> 12 v dt, EXACTAMENTE el de siempre (25/26 tomas byte a
    #      byte). Tras N filas rechazadas el vehiculo avanzo N frames y el tope lo sabe.
    #  (2) UN GLITCH ES UN SALTO DEL QUE LA TOMA VUELVE: si en REBOB_TOPE_S ninguna fila vuelve a
    #      quedar dentro del tope de la referencia, no era un pico sino la trayectoria -> se acepta
    #      y se AVISA. Si se acaban los datos antes de saberlo, se descarta como siempre (aceptar
    #      un pico en los ultimos frames ensuciaria el ENDPOINT).
    # Medido en el F350: rebobinado -15,79 m (fila 8417) -> se aceptaba en 8433 una fila todavia
    # DETRAS -> puesta al dia +10,48 m > tope 9,21 (8463) -> referencia congelada y tope fijo:
    # 3436 filas, 86 s, ~730 m tirados SIN AVISO (cinta 1513 m contra polilinea 2272). Ahora 2284.
    # ✖ Solo (2) NO alcanza (sintetico: pico de 30 filas a 50 m que vuelve): el vehiculo avanzo 12,9 m
    #   durante el pico, ninguna fila vuelve al tope fijo de 5 m -> se aceptaba el pico como
    #   "persistente" y quedaban 30 filas desplazadas en la cinta. El codigo viejo ahi CASCADEABA.
    # ✖ Solo (1) NO alcanza: un salto persistente (+30 m que no vuelve) se aleja a la misma velocidad
    #   con la que crece el tope -> cascada igual.
    # ✖ "rebobinados antes del filtro" (la otra opcion de ): mismas filas en las 26 tomas,
    #   pero no toca la referencia rancia -- un empalme un poco peor volvia a cascadear.
    glitches = 0
    avisos = []
    racha_n = 0
    racha_ini = 0
    transc = 0.0   # segundos desde la referencia (el ultimo frame retenido)
    limpias = [filas[0]]
    for ig in range(1, len(filas)):
        fr = filas[ig]
        p = limpias[-1]
        paso = ((fr["x"] - p["x"]) ** 2 + (fr["z"] - p["z"]) ** 2) ** 0.5
        dtg = fr["dt"]
        if dtg <= 0:
            dtg = 0.025
        # ⚠ 12x, no 5x: a 190-211 km/h el canal de posicion se congela
        # ~5-7 frames y se pone al dia con UN salto de 9-11.6 m (medido en la
        # jesko, 5 clusters) — eso es movimiento REAL reportado en rafaga, no un
        # teleport. Descartarlo dejaba HUECOS cuyo chord recto desincronizaba la
        # odometria del lector de golpe (saltos de distM 6.8-14.8 en el RPT; el
        # de 14.8 supero el alcance del rescate y quedo en fallback 58 muestras a
        # 175). Un teleport real (respawn, cientos de metros) sigue muriendo.
        transcurrido = transc + dtg
        tope = (fr["ckmh"] / 3.6) * (transcurrido + 11.0 * dtg)
        if tope < CINTA_GLITCH_M:
            tope = CINTA_GLITCH_M
        if paso > tope:
            vuelve = False
            agoto = False
            tq = transcurrido
            jq = ig + 1
            while jq < len(filas):
                q = filas[jq]
                dtq = q["dt"] if q["dt"] > 0 else 0.025
                tq += dtq
                topeq = (q["ckmh"] / 3.6) * (tq + 11.0 * dtq)
                if topeq < CINTA_GLITCH_M:
                    topeq = CINTA_GLITCH_M
                if ((q["x"] - p["x"]) ** 2 + (q["z"] - p["z"]) ** 2) ** 0.5 <= topeq:
                    vuelve = True
                    break
                if tq - transcurrido > REBOB_TOPE_S:
                    agoto = True
                    break
                jq += 1
            if vuelve or not agoto:
                glitches += 1
                transc = transcurrido
                if racha_n == 0:
                    racha_ini = ig
                racha_n += 1
                continue
            avisos.append("[cinta]   (!) salto persistente de %.2f m en la fila %d: no vuelve en %.0f s -> NO es glitch, se acepta"
                          % (paso, ig, REBOB_TOPE_S))
        if racha_n >= GLITCH_RACHA_AVISO:
            avisos.append("[cinta]   (!) glitch en racha: %d filas desde la fila %d" % (racha_n, racha_ini))
        racha_n = 0
        transc = 0.0
        limpias.append(fr)
    if racha_n >= GLITCH_RACHA_AVISO:
        avisos.append("[cinta]   (!) glitch en racha: %d filas desde la fila %d (hasta el final de la toma)" % (racha_n, racha_ini))
    antes = len(limpias)
    limpias, rebob, rebob_abort = descartar_rebobinados(limpias)
    # ⚠⚠⚠ EL EJE s: ARCO DE POSICIONES (la escala que MIDE el consumidor -- el
    # server avanza por delta-posicion), INTERPOLADO POR PESOS DE VELOCIDAD dentro
    # de las mesetas. Dos mediciones lo forzaron (espejo, toma 06):
    #  (1) la POSICION del frame esta cuantizada ~10-16 Hz (paso 0.000 exacto por
    #      8 frames con chassis_kmh 12 y la rueda barriendo 2 deg/frame): el arco
    #      crudo de posiciones tiene MESETAS justo donde la textura es fina;
    #  (2) chassis_kmh SOBRE-LEE 5-17% (media ~11%) contra el desplazamiento real
    #      (medido por ventanas de 200 frames; no es 3D ni constante): integrar
    #      v*dt da un eje 12% mas largo que el que el consumidor recorre.
    # Solucion: ANCLAS donde la posicion realmente avanzo fijan la escala; entre
    # anclas, s se distribuye por el peso v*dt de cada frame -> resolucion 40 Hz
    # honesta + escala honesta.
    # ⚠⚠⚠ Y LA ESCALA ES LA CUERDA ANCLA-A-ANCLA, NO la suma de micro-pasos
    # el jitter de cuantizacion (pasos 0.005-0.05 que van y vienen)
    # sumado en crudo infla el arco con zigzag que el vehiculo real NO recorre --
    # medido en la 06: +2.7 m de arco fantasma en s 55-106 (zona lenta), que
    # desincronizaba la odometria del lector (el vehiculo avanza la CUERDA) y
    # metia fw p90 4 deg justo donde no hay anclas de re-sincronizacion. El
    # umbral de ancla sube a 0.06 m (2x el techo del jitter ~0.05): la cuerda
    # entre anclas asi de cercanas es el desplazamiento neto real.
    npts = len(limpias)
    svdt = [0.0] * npts
    i2 = 1
    while i2 < npts:
        svdt[i2] = svdt[i2 - 1] + (limpias[i2]["ckmh"] / 3.6) * limpias[i2]["dt"]
        i2 += 1
    anclas = [0]
    i2 = 1
    while i2 < npts:
        fa = limpias[anclas[-1]]
        fb = limpias[i2]
        if ((fb["x"] - fa["x"]) ** 2 + (fb["z"] - fa["z"]) ** 2) ** 0.5 > 0.06:
            anclas.append(i2)
        i2 += 1
    if anclas[-1] != npts - 1:
        anclas.append(npts - 1)
    # escala en las anclas: cuerda acumulada ancla-a-ancla (desplazamiento NETO)
    sank = [0.0] * len(anclas)
    for k2 in range(1, len(anclas)):
        fa = limpias[anclas[k2 - 1]]
        fb = limpias[anclas[k2]]
        sank[k2] = sank[k2 - 1] + ((fb["x"] - fa["x"]) ** 2 + (fb["z"] - fa["z"]) ** 2) ** 0.5
    svals = [0.0] * npts
    k2 = 0
    for i2 in range(npts):
        while k2 < len(anclas) - 2 and anclas[k2 + 1] <= i2:
            k2 += 1
        a2 = anclas[k2]
        b2 = anclas[k2 + 1]
        dv = svdt[b2] - svdt[a2]
        t2 = 0.0
        if dv > 1e-9:
            t2 = (svdt[i2] - svdt[a2]) / dv
            if t2 < 0.0: t2 = 0.0
            if t2 > 1.0: t2 = 1.0
        svals[i2] = sank[k2] + (sank[k2 + 1] - sank[k2]) * t2
    with open(out_path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["s", "x", "y", "z", "heading", "chassis_kmh", "front_wheel_deg",
                    "throttle", "brake", "gear", "handbrake", "dt"])
        for i2 in range(npts):
            fr = limpias[i2]
            w.writerow([round(svals[i2], 3), fr["x"], fr["y"], fr["z"], fr["hdg"], fr["ckmh"],
                        fr["fw"], fr["thr"], fr["brk"], fr["gear"], fr["hb"], fr["dt"]])
    s = svals[npts - 1]
    print("[cinta] %s: %d frames (glitches descartados: %d) · arco total %.1f m"
          % (os.path.basename(out_path), len(limpias), glitches, s))
    if rebob or rebob_abort:
        print("[cinta]   rebobinados descartados: %d (%d filas del tramo duplicado)%s"
              % (rebob, antes - len(limpias),
                 "  · %d NO reparados: no volvio a pasar el punto en %.0f s" % (rebob_abort, REBOB_TOPE_S) if rebob_abort else ""))
    for aviso in avisos:
        print(aviso)
    # (regla B4): las detenciones grabadas SIN marca de parada se INFORMAN, como los trompos y los saltos.
    # No se editan (la toma se convierte tal cual): el framework las reproduce por tiempo (meseta con salida).
    for aviso in detenciones_sin_marca(limpias, svals):
        print(aviso)
    # cuantas filas de DATOS se escribieron (sin el header). El server lo usa como
    # denominador del % de carga del panel.
    return npts


# 35 detenciones del humano en RUTA02 sin marca de parada (paro a marcar y de la marca no quedo nada):
# v grabada 0 durante 0,15-6 s. Antes eran invisibles y en el server cada una era una ruleta (bus clavado en Vybor).
# Se informan; no se tocan. DET_FRAMES_MIN = 8 (0,2 s): por debajo es un roce del umbral, no una detencion.
DET_KMH        = 1.0   # 'por debajo de lo que el vehiculo puede arrancar'
DET_FRAMES_MIN = 8
DET_MARCA_VENT = 80    # filas (2 s) alrededor: una marca ahi = la detencion es una parada declarada

def detenciones_sin_marca(limpias, svals):
    avisos = []
    n = len(limpias)
    i = 0
    con_marca = 0
    while i < n:
        if limpias[i]["ckmh"] >= DET_KMH:
            i += 1
            continue
        j = i
        while j + 1 < n and limpias[j + 1]["ckmh"] < DET_KMH:
            j += 1
        largo = j - i + 1
        if largo >= DET_FRAMES_MIN and i > 0 and j < n - 1:
            marca = any(limpias[k].get("mark", 0) != 0 for k in range(max(0, i - DET_MARCA_VENT), min(n, j + DET_MARCA_VENT + 1)))
            if marca:
                con_marca += 1
            else:
                avisos.append("[cinta]   (!) detencion grabada SIN marca de parada: %.1f s (%d frames) en s %.1f (filas %d-%d): el server la reproduce por tiempo"
                              % (largo / 40.0, largo, svals[i], i, j))
        i = j + 1
    if avisos:
        avisos.append("[cinta]   %d detencion(es) sin marca de parada (%d con marca)" % (len(avisos), con_marca))
    return avisos


def load_frames(frame_path):
    """Lee el frame_*.csv por nombres de columna. is_marker opcional (formato viejo)."""
    rows = []
    with open(frame_path, encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            try:
                rows.append({
                    "t": float(r.get("t_cum", 0) or 0),
                    "x": float(r["x"]), "y": float(r["y"]), "z": float(r["z"]),
                    "spd": float(r["speed_kmh"]), "gear": int(float(r["gear"])),
                    # PEDAL GRABADO (2026-08-27): el frame lo trae y el conversor lo tiraba.
                    # Es la medicion mas directa de "que pedal logra esta velocidad ACA", y es
                    # por-waypoint = espacial: no reintroduce ningun cursor de tiempo. Se
                    # captura siempre; escribirlo o no lo decide --keep-inputs.
                    "thr": float(r.get("throttle", 0) or 0),
                    "brk": float(r.get("brake", 0) or 0),
                    "hdg": float(r["heading"]),
                    # ANGULO REAL DE RUEDA (2026-07-20): el recorder lo mide con WheelGetDirection y el
                    # frame CSV lo trae, pero el conversor lo venia DESCARTANDO -> las rutas salian con
                    # col18=0 -> BZVehicleEnvelope no podia aprender el understeer k(v) del vehiculo y
                    # Boris viraba con el k=0.90 inventado. Medido en el M3: envelope con 0 muestras,
                    # se abria 1.9 m en la aproximacion y salia 64 deg torcido de la reversa.
                    "fw": float(r.get("front_wheel_deg", 0) or 0),
                    "brake": float(r.get("brake", 0) or 0),
                    "mark": int(float(r.get("is_marker", 0) or 0)),
                    # los dos campos que lee descartar_rebobinados, de la MISMA fuente
                    # que emitir_cinta (chassis_kmh con respaldo speed_kmh; dt del frame). No se escriben.
                    "ckmh": float(r.get("chassis_kmh", 0) or r["speed_kmh"]),
                    "dt": float(r.get("dt", 0) or 0),
                })
            except (ValueError, KeyError):
                pass
    return rows

# (el usuario, 22/09): EL CREEP VIAJA EN LA TOMA. Se deriva del coast2 del envelope de la clase
# (profiles\BZ_AutoDrive\envelope_<VehicleClass>.csv) con la MISMA regla que el motor (BZEnvelopeStore.CoastEnReposo):
# los 3 bins mas bajos con >= 5 muestras, ponderado por muestras, piso duro (idx 0-19) y si no tiene bins el blando
# (20-39); decel > 0 = frena aun casi parado = NO REPTA (creep 0); < 0 = el cruce por cero entre centros de bin
# (bins de 5 km/h). Sin envelope o sin coast2 el hdr NO lleva los campos: el motor deriva al cargar. Una sola verdad.
COAST_NBINS = 20
COAST_BINW = 5.0
COAST_REPOSO_BINS = 3
COAST_REPOSO_MIN = 5

def leer_coast2(env_path):
    coast = {}
    if not os.path.exists(env_path):
        return coast
    with open(env_path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            p = ln.strip().split(",")
            if len(p) >= 4 and p[0] == "coast2":
                try:
                    coast[int(p[1])] = (float(p[2]), int(p[3]))
                except ValueError:
                    pass
    return coast

def coast_en_reposo(coast):
    """(decel ponderada, muestras, superficie) de los 3 bins mas bajos con >= 5 muestras; None si no hay."""
    for s in (0, 1):
        suma = 0.0
        cnt = 0
        usados = 0
        for b in range(COAST_NBINS):
            if usados >= COAST_REPOSO_BINS:
                break
            idx = s * COAST_NBINS + b
            if idx not in coast or coast[idx][1] < COAST_REPOSO_MIN:
                continue
            suma += coast[idx][0]
            cnt += coast[idx][1]
            usados += 1
        if usados > 0:
            return suma / cnt, cnt, s
    return None

def creep_por_cruce(coast, s):
    d_prev = 0.0
    c_prev = -1.0
    for b in range(COAST_NBINS):
        idx = s * COAST_NBINS + b
        if idx not in coast or coast[idx][1] < COAST_REPOSO_MIN:
            continue
        d = coast[idx][0] / coast[idx][1]
        c = (b + 0.5) * COAST_BINW
        if d >= 0 and c_prev >= 0:
            return c_prev + (c - c_prev) * (0.0 - d_prev) / (d - d_prev)
        d_prev = d
        c_prev = c
    if c_prev < 0:
        return 0.0
    return c_prev

def envelope_lineas(env_path):
    """Las lineas no vacias de envelope_<clase>.csv, tal cual (B6: el envelope entero viaja en el hdr). Una linea con comillas
    o barras invertidas romperia el JSON y el lector del motor (que corta por comillas): se descarta con aviso."""
    if not os.path.exists(env_path):
        return []
    out = []
    with open(env_path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            s = ln.strip()
            if not s:
                continue
            if '"' in s or "\\" in s:
                print("  envelope: linea descartada del hdr (comillas o barra): %s" % s[:60])
                continue
            out.append(s)
    return out

def creep_desde_envelope(env_path):
    """(CreepKmh, CreepSource, CreepSamples, CoastAtRest) o None si el envelope no tiene coast2 en reposo."""
    coast = leer_coast2(env_path)
    r = coast_en_reposo(coast)
    if r is None:
        return None
    decel, n, s = r
    if decel > 0:
        return 0.0, "coast2", n, round(decel, 4)
    return round(creep_por_cruce(coast, s), 3), "coast2", n, round(decel, 4)

def endpoint_brake_decel(rows):
    # Desac. REAL del frenado final (m/s2): el humano hace carry+slam -> lleva velocidad y clava el freno tarde.
    # Medimos desde el punto donde CLAVA (brake>0.8 sostenido, dentro de 8m del final) hasta el stop: dec=v^2/2d.
    # NO es f(masa) (Huracan 6.9 vs f22 1.3) -> hay que MEDIRLA. El iman la reproduce (v_target + freno predictivo).
    n = len(rows)
    if n < 10:
        return 0.0
    de = [0.0] * n
    for i in range(n - 2, -1, -1):
        de[i] = de[i + 1] + math.hypot(rows[i + 1]["x"] - rows[i]["x"], rows[i + 1]["z"] - rows[i]["z"])
    slam = None
    for i in range(n):
        if de[i] < 8.0 and rows[i]["brake"] > 0.8:
            slam = i
            break
    if slam is None:
        return 0.0
    v0 = rows[slam]["spd"] / 3.6
    d0 = de[slam]
    if d0 < 0.3 or v0 < 1.0:
        return 0.0
    dec = (v0 * v0) / (2.0 * d0)
    return max(0.5, min(8.0, dec))

def is_reverse(row, prev):
    """Reversa. PRIMARIO: gear==0 (CarGear.REVERSE, senal real que loguea el recorder).
    BACKUP: movimiento neto hacia atras vs heading, PERO solo a baja velocidad y con
    desplazamiento real (a alta vel nunca hay reversa; un glitch de 2 frames casi
    iguales daba falsos positivos a 105 km/h)."""
    if row["gear"] == 0:
        return True
    if prev is None or row["spd"] > REV_MAXKMH:
        return False
    dx, dz = row["x"] - prev["x"], row["z"] - prev["z"]
    mv = math.hypot(dx, dz)
    if mv < REV_MINMOVE:
        return False
    fx = math.sin(math.radians(row["hdg"]))   # forward unit (x,z) del heading
    fz = math.cos(math.radians(row["hdg"]))
    return (dx * fx + dz * fz) / mv < REV_DOT

RETRO_UMBRAL_M = 2.0   # el del motor (BZRouteData.RETRO_UMBRAL_M): >2 m contra el rumbo = glitch

def sanear_retrocesos(keep, WP):
    """Espejo EXACTO de BZRouteData.EsRetroceso, aplicado AL CONVERTIR: un glitch
    de captura (salto atras de metros) iba crudo al _wp.csv y solo el runtime lo enmascaraba
    AL CARGAR -> el archivo en disco quedaba sucio (editor, analisis, un build sin ese filtro
    lo ven). Filtrado aca, disco y motor ven lo mismo; el del runtime queda de cinturon.
    ⚠ La direccion de avance sale del RUMBO GRABADO del ultimo punto ACEPTADO (+180 en
    reversa), NUNCA de los dos ultimos puntos: la version dos-puntos CASCADEABA (se comia 451
    wps del 07-Agera; el espejo con paridad la freno antes de shippear). Exentos: legBreak,
    isStop y cambio de sentido -- las mismas compuertas del motor. keep y WP se filtran en
    PARALELO para que los eventos (que indexan por posicion) queden coherentes."""
    keepF = [keep[0]]
    wpF = [WP[0]]
    n = 0
    peor = 0.0
    for i in range(1, len(WP)):
        w = WP[i]
        ult = wpF[-1]
        if not (w.get("legBreak") or w["isStop"] or w["mode"] != ult["mode"]):
            h = math.radians(ult["targetHeading"])
            if ult["mode"] == "reverse":
                h += math.pi          # en reversa la marcha va al reves del yaw
            avance = ((w["pos"][0] - ult["pos"][0]) * math.sin(h)
                      + (w["pos"][2] - ult["pos"][2]) * math.cos(h))
            if avance <= -RETRO_UMBRAL_M:
                n += 1
                if -avance > peor:
                    peor = -avance
                continue
        keepF.append(keep[i])
        wpF.append(w)
    return keepF, wpF, n, peor

# ⚠⚠ TROMPO: una toma donde el vehiculo GIRA SOBRE SI MISMO no es
# reproducible -- ningun chofer sigue la trayectoria de un derrape sin derrapar. Caso real: SUZUKI,
# fila 7393: 3ra puesta, volante en 0, el chasis gira 184 grados en 2 s y la moto sigue 8 m de
# culata; el conversor lo leia como un leg de REVERSA de 1,3 m y la ruta quedaba INCOMPLETE.
# Se AVISA y NO se corta: un recorte automatico cambia el endpoint sin que nadie lo pida.
# ⚠ DOS condiciones, porque cada una sola da falsos avisos (medido en 26 tomas):
#   - ROTACION del chasis > 90 grados en 2 s (lo pedido): la esquina mas cerrada de la flota llega a
#     85,9 (SPEED08b, 19 km/h) -- 4 grados de margen; una rotonda de r=10 m a 30 km/h gira ~95.
#     Sola le diria "regraba" a una toma buena.
#   - DESLIZAMIENTO (rumbo del chasis contra direccion del movimiento) > 60 grados sostenido: en esa
#     esquina es 15. Pero solo tampoco sirve: en SPEED01 (t 179,8 s) el chasis apunta a +z, marcha 6,
#     28 km/h en el tablero, y z BAJA 1,7 m -- probable rebobinado lento, bajo el umbral de -2 m por
#     fila -- y da 177 grados durante 24 filas sin girar nada.
#   SUZUKI tiene las dos (184 grados / >60 durante 104 frames); ninguna otra toma tiene las dos.
# ✖ La direccion del movimiento sale de x,z y se mide SOBRE LAS FILAS SIN REBOBINADOS: sobre la toma
# cruda los rebobinados dan picos de 180 grados (F350, HX58, DEMOBUS) -- el motivo por el que
#   la habia descartado; sin rebobinados quedan en 14-18. Honesto: esos picos duran 4-6 filas, y con
#   TROMPO_DESLIZ_FILAS = 10 tampoco pasarian (mutante 'sobre la toma cruda' VIVO en las 26 tomas).
#   Es defensa en profundidad, no lo que discrimina hoy.
# ✖ NO se reclasifica como avance lo que viene despues del trompo: la toma no es reproducible desde ahi
#   de ninguna forma, y cambiar legs sin arreglar nada solo cambiaria archivos. El aviso le dice al
#   modder donde cortar.
TROMPO_GIRO_DEG      = 90.0
TROMPO_VENTANA_S     = 2.0
TROMPO_VMIN_KMH      = 15.0
TROMPO_DESLIZ_DEG    = 60.0
TROMPO_DESLIZ_FILAS  = 10     # 0,25 s: los picos sueltos de cuantizacion duran 1-2 filas (WRX)
TROMPO_DESLIZ_M      = 1.0    # desplazamiento para medir la direccion del movimiento

def detectar_trompos(filas):
    """Devuelve [{desde, hasta, giro, desliz, filas_desliz}] con indices sobre `filas` (sin rebobinados)."""
    n = len(filas)
    if n < 3:
        return []
    def envolver(a):
        while a > 180.0:
            a -= 360.0
        while a < -180.0:
            a += 360.0
        return a
    rumbo = [filas[0]["hdg"]]
    tiempo = [0.0]
    arco = [0.0]
    for i in range(1, n):
        rumbo.append(rumbo[-1] + envolver(filas[i]["hdg"] - filas[i - 1]["hdg"]))
        tiempo.append(tiempo[-1] + (filas[i]["dt"] if filas[i]["dt"] > 0 else 0.025))
        arco.append(arco[-1] + math.hypot(filas[i]["x"] - filas[i - 1]["x"], filas[i]["z"] - filas[i - 1]["z"]))
    def rodando(r):
        return r["ckmh"] > TROMPO_VMIN_KMH and r["gear"] >= 2
    desliz = [None] * n
    ja = 0
    ka = 0
    for i in range(n):
        if not rodando(filas[i]):
            continue
        while ja < i and arco[i] - arco[ja + 1] >= TROMPO_DESLIZ_M / 2:
            ja += 1
        while ka < n - 1 and arco[ka] - arco[i] < TROMPO_DESLIZ_M / 2:
            ka += 1
        dx = filas[ka]["x"] - filas[ja]["x"]
        dz = filas[ka]["z"] - filas[ja]["z"]
        if math.hypot(dx, dz) < TROMPO_DESLIZ_M * 0.8:
            continue
        desliz[i] = envolver(filas[i]["hdg"] - math.degrees(math.atan2(dx, dz)))
    eventos = []
    j0 = 0
    for i in range(n):
        while tiempo[i] - tiempo[j0] > TROMPO_VENTANA_S:
            j0 += 1
        if not rodando(filas[i]):
            continue
        desde = None
        jj = i - 1
        while jj >= j0 and rodando(filas[jj]):
            if abs(rumbo[i] - rumbo[jj]) > TROMPO_GIRO_DEG:
                desde = jj
            jj -= 1
        if desde is None:
            continue
        if eventos and desde <= eventos[-1]["hasta"]:
            eventos[-1]["hasta"] = i
        else:
            eventos.append({"desde": desde, "hasta": i})
    trompos = []
    for ev in eventos:
        fin = ev["hasta"]
        while fin < n - 1 and tiempo[fin] - tiempo[ev["hasta"]] < TROMPO_VENTANA_S:
            fin += 1          # el derrape SIGUE al giro: se mira tambien lo que viene despues
        racha = 0
        mejor = 0
        pico = 0.0
        for q in range(ev["desde"], fin + 1):
            d = desliz[q]
            if d is not None and abs(d) > TROMPO_DESLIZ_DEG:
                racha += 1
                if racha > mejor:
                    mejor = racha
                if abs(d) > pico:
                    pico = abs(d)
            else:
                racha = 0
        if mejor >= TROMPO_DESLIZ_FILAS:
            giro = max(abs(rumbo[q] - rumbo[ev["desde"]]) for q in range(ev["desde"], fin + 1))
            trompos.append({"desde": ev["desde"], "hasta": fin, "giro": giro, "desliz": pico, "filas_desliz": mejor})
    return trompos

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frame")
    ap.add_argument("route_name")
    # ⚠⚠ OPT-IN A PROPOSITO. `hasInputData` NO es un campo muerto: la v1.1 PUBLICADA lo usa
    # para reproducir el pedal grabado. Escribir 1 por defecto le cambiaria la conducta a v1.1
    # en toda ruta nueva -- un efecto colateral sobre un mod publicado, decidido de refilon por
    # un cambio pensado para v1.2. Con la bandera, quien convierte para v1.2 la pide.
    ap.add_argument("--keep-inputs", action="store_true",
                    help="conserva el pedal grabado (throttle/brake) y marca hasInputData=1, "
                         "para el feedforward de v1.2. Sin esto, conducta identica a siempre.")
    # OBLIGATORIO: el conversor no adivina donde esta el server de nadie (el Wizard lo pasa siempre).
    ap.add_argument("--profile", default=None, metavar="DIR",
                    help="carpeta de rutas del server (.../profiles/BZ_AutoDrive) donde se deploya la ruta. Obligatorio.")
    # ★★★ LA CINTA (E1, ): la toma FULL-RES como overlay de runtime,
    # reparametrizada por ARCO, para el ejecutor de E2 (BZCintaRunner). La ruta
    # decimada sigue siendo la de planificacion. Aditivo: sin bandera, identico.
    ap.add_argument("--cinta", action="store_true",
                    help="emite <base>_cinta.csv FULL-RES (40Hz nativo) indexada por arco, "
                         "con la RUEDA LOGRADA (front_wheel_deg) + gas/freno/marcha")
    # ★ SACAR UNA MARCA SIN TOCAR LA TOMA (el usuario, 23/09: "la marque al final, no va").
    # Una marca de mas se corrige desde ACA, no editando el frame_*.csv ni el hdr.json ya
    # convertido: la toma cruda es el dato y no se toca, y un hdr editado a mano vuelve a traer
    # la marca en la proxima conversion. Con la bandera, la conversion es REPRODUCIBLE.
    # Se numeran 1..N en orden de tiempo, igual que las lista `revisar_toma.py`.
    # ★ PONER una marca donde la toma no la tiene, por WAYPOINT de salida (el usuario, 23/09: "la marca
    # deberia estar unos 10 metros del final, la podes poner?").
    # El caso: la mano que VUELVE de la costera tiene que cederle el paso al ramal en Rify, pero su
    # parada queda a 12 m del paso del ramal -- esperar ahi es esperar adentro de la zona. El lugar
    # correcto esta ~10 m antes, donde la cinta ya viene a 14 km/h frenando y la separacion sube a
    # 21 m. Regrabar 2,3 km por una marca no tiene sentido, y editar el hdr a mano se pierde en la
    # proxima conversion: va como bandera, y la conversion queda REPRODUCIBLE.
    # ⚠ El indice es del WAYPOINT DE SALIDA (el de `<base>_wp.csv`), no del frame.
    ap.add_argument("--marca-wp", type=int, nargs="+", metavar="W", default=[],
                    help="agrega una marca (Event) en esos waypoints de la ruta de salida")
    ap.add_argument("--sin-marca", type=int, nargs="+", metavar="N", default=[],
                    help="ignora la marca numero N (1..N en orden de tiempo, como las numera "
                         "revisar_toma.py). Se puede repetir: --sin-marca 2 5")
    args = ap.parse_args()
    if not args.profile:
        ap.error("falta --profile <carpeta profiles/BZ_AutoDrive de TU server>: el conversor no adivina donde esta el server "
                 "(el Wizard lo pasa solo)  /  --profile is required (your server's profiles/BZ_AutoDrive folder)")

    hk   = load_header(args.frame)
    rows = load_frames(args.frame)
    if len(rows) < 2:
        raise SystemExit("[ERROR] grabacion vacia o ilegible: " + args.frame)

    # ★ --sin-marca: se aplica ANTES de todo lo demas, porque la marca alimenta dos caminos
    # distintos -- declarar la parada (preservar_paradas fuerza 0 km/h en su bloque detenido) y
    # la lista de Events del hdr. Borrandola aca, los dos quedan consistentes.
    if args.sin_marca:
        idx = [i for i, r in enumerate(rows) if r.get("mark", 0) != 0]
        print("[marcas] la toma trae %d marca(s)" % len(idx))
        pedidas = sorted(set(args.sin_marca))
        for n in pedidas:
            if n < 1 or n > len(idx):
                raise SystemExit("[ERROR] --sin-marca %d: la toma tiene %d marca(s)" % (n, len(idx)))
        # ⚠ una "marca" puede ocupar varias filas seguidas: se borra el bloque contiguo entero,
        # si no queda un resto que sigue declarando la parada.
        for n in pedidas:
            i = idx[n - 1]
            j = i
            while j >= 0 and rows[j].get("mark", 0) != 0:
                rows[j]["mark"] = 0; j -= 1
            j = i + 1
            while j < len(rows) and rows[j].get("mark", 0) != 0:
                rows[j]["mark"] = 0; j += 1
            print("[marcas] IGNORADA la marca %d (t %.1f s, %.0f,%.0f)"
                  % (n, rows[i]["t"] - rows[0]["t"], rows[i]["x"], rows[i]["z"]))

    # rebobinados FUERA antes de decimar ---
    # ⚠ La cinta ya los sacaba y la polilinea de waypoints NO: `resample` suma
    # hypot() entre filas consecutivas, asi que el salto hacia atras de 9-15 m del rubber-band
    # CONTABA COMO AVANCE y emitia waypoints sobre el tramo duplicado. sanear_retrocesos se
    # comia los que retrocedian >= 2 m y dejaba los de 1-2 m: zigzag hacia atras + hueco
    # (SUZUKI: 38 wps descartados). `remainM`, `nearWp` y la promocion a ENDPOINT salen de esa
    # polilinea. Misma funcion de rebobinado que la cinta. ⚠ SIN el filtro de glitch de la cinta:
    # la polilinea nunca lo tuvo, y ese filtro CASCADEA en el F350 (: tira las ultimas
    # 3436 filas tras un rebobinado + rafaga de puesta al dia) -- no se copia un bug abierto.
    # ✖ `rows` NO se reemplaza: MaxGear y EndpointBrakeDecel (header) siguen leyendo la toma
    # cruda. Cambiarlos es otra decision, no la de este arreglo.
    filas_wp, wp_rebob, _wp_rebob_abort = descartar_rebobinados(rows)
    if wp_rebob:
        print("  [wp] rebobinados descartados: %d (%d filas del tramo duplicado)" % (wp_rebob, len(rows) - len(filas_wp)))

    # --- resampleo a resolucion fisica (mata el micro-jitter que fingia curvatura -> zigzag) ---
    keep = resample(filas_wp)

    # --- construir waypoints ---
    WP = []
    for i, w in enumerate(keep):
        prev = keep[i - 1] if i > 0 else None
        rev  = is_reverse(w, prev)
        WP.append({
            "pos": [round(w["x"], 3), round(w["y"], 3), round(w["z"], 3)],
            "isStop": False, "name": "", "stopDuration": 0, "stopRadius": 0.0,
            "targetSpeed": 0.0 if w.get("es_parada") else round(w["spd"], 2), "targetGear": int(w["gear"]),
            "targetThrottle": round(w.get("thr", 0.0), 3) if args.keep_inputs else 0,
            "targetBrake": round(w.get("brk", 0.0), 3) if args.keep_inputs else 0,
            "targetHandbrake": 0, "targetSteering": 0,
            "mode": "reverse" if rev else "normal", "hasInputData": bool(args.keep_inputs),
            "targetHeading": round(w["hdg"], 3),
            "targetFrontWheel": round(w.get("fw", 0.0), 3),
            # INTERCAMBIO (is_marker=2): corte de tramo DECLARADO por el autor (marcador manual; la vieja
            # tecla NUMPAD 3/UABZMarkLeg se saco 2026-08-11 -> hoy se AUTO-DETECTA del gear, ver abajo).
            # Boris trata ese wp como start point. No se infiere de un 0 km/h: un cero solo es una PAUSA.
            "legBreak": w["mark"] == 2,
        })

    # AUTO-INTERCAMBIO (2026-08-10, un jugador): el intercambio = cambio de SENTIDO (forward<->reverse), que
    # SIEMPRE cae a velocidad ~0 (no se pasa de forward-en-movimiento a reverse sin parar). Esas DOS
    # condiciones juntas (vel 0 + cambio de sentido) lo distinguen de una simple PAUSA (un 0 SIN cambio de
    # sentido = pausa, no intercambio). Se auto-deriva del gear -> el marcador manual (is_marker=2) queda
    # OPCIONAL (override: el mark==2 de arriba se une a esto). Ademas declara el tramo inicial si la toma ARRANCA en reversa
    # (sino el control no arma el tramo y Boris no despega). Reproduce el patron de los legBreak manuales.
    if WP and WP[0]["mode"] == "reverse":
        WP[0]["legBreak"] = True
    for i in range(1, len(WP)):
        if (WP[i - 1]["mode"] == "reverse") != (WP[i]["mode"] == "reverse"):
            WP[i - 1]["legBreak"] = True   # borde: ultimo wp del tramo saliente (donde para para cambiar)

    # endpoint: si termina ~parado, clava isStop -> FollowPaintedToStop mete vKmh=0
    if keep[-1]["spd"] <= STOP_KMH:
        WP[-1]["isStop"] = True
        WP[-1]["targetSpeed"] = 0.0
        WP[-1]["stopDuration"] = STOP_HOLD_S   # hold -> el creep fino clava el punto antes de despawn

    # RAMPA DE APROXIMACION AL STOP (fix endpoint 2026-07-13): las frenadas grabadas suelen ser
    # ABRUPTAS al final (el humano frena SOBRE el punto: 16->0 en ~1.7m). Boris llega a la zona de
    # arribo (2.5-3m) todavia a ~10-16 km/h -> la ruta se completa/despawnea 2.5m ANTES del punto,
    # justo donde vive la rampa a 0 -> nunca clava el 0.00. Fix: condicionar la aproximacion con una
    # decel-limit backward pass (constante ENDPOINT_DECEL) para que Boris llegue a <5 km/h en los
    # ultimos ~3m -> ahi el creep fino de AtStop clava el punto (como NUEVO03 = 0.06m). Solo BAJA la
    # velocidad del tramo final (min con lo grabado) -> el resto del perfil queda intacto. un jugador lo
    # autorizo: "no importa fuerte o lento, pero clava el 0.00".
    # LOS INTERCAMBIOS (legBreak) SON ENDPOINTS (2026-07-26, un jugador: "llevar interc1 y 2 al nivel del endpoint,
    # en definitiva son eso"). Boris tiene que LLEGAR PARADO y clavar la pose ahi (ademas para forward<->reverse
    # DEBE parar) -> misma rampa de frenado que el endpoint. Sin esto el legBreak llegaba con velocidad (medido
    # MERGE: 0.9 km/h interc1, 2.4 interc2) SIN rampa suave -> Boris se pasaba y cerraba a ~1.5m en vez de 0.4m.
    for si in range(len(WP)):
        if not (WP[si]["isStop"] or WP[si].get("legBreak")):
            continue
        if WP[si].get("legBreak"):
            WP[si]["targetSpeed"] = 0.0   # parar EN el intercambio (como el endpoint) -> el creep clava la pose
        y_end = WP[si]["pos"][1]
        d = 0.0
        for j in range(si - 1, -1, -1):
            if WP[j].get("legBreak") or WP[j]["isStop"]:
                break   # no cruzar otro intercambio/parada hacia atras
            dx = WP[j]["pos"][0] - WP[j + 1]["pos"][0]
            dz = WP[j]["pos"][2] - WP[j + 1]["pos"][2]
            d += math.hypot(dx, dz)
            if d > ENDPOINT_RAMP_M:
                break
            # DECEL EFECTIVA SEGUN PENDIENTE (2026-07-25, un jugador: "tiene que venir de abajo pisando el
            # acelerador, con envion; si mete gas en la cuesta hace pasitos"). Si el punto de parada esta CUESTA
            # ARRIBA, la gravedad YA frena -> la decel efectiva es mayor -> la rampa PERMITE mas velocidad
            # (no aplasta el envion que Boris necesita para trepar; medido T2: la rampa plana bajaba la v
            # objetivo de los 18 km/h grabados a 9). sin(theta) ~= desnivel/distancia del punto j al punto.
            # En bajada (grade<0) pasa lo inverso: menos decel -> frena antes (la gravedad acelera).
            grade = (y_end - WP[j]["pos"][1]) / d if d > 0.1 else 0.0
            decel_eff = ENDPOINT_DECEL + 9.8 * grade
            if decel_eff < 0.35:   # piso subido 0.1->0.35 (2026-07-31): en bajada fuerte la rampa no baja de ~8.5 km/h
                decel_eff = 0.35   # a 8m -> Boris no crawlea a 5-6 (donde oscila). El iman clava la parada.
            ramp_kmh = math.sqrt(2.0 * decel_eff * d) * 3.6
            if ramp_kmh < WP[j]["targetSpeed"]:
                WP[j]["targetSpeed"] = round(ramp_kmh, 2)

    # ARRANQUE PARADO EN REVERSA (2026-07-25, un jugador: "Boris spawnea girado / se cae por la pendiente"). Si la
    # toma arranca DETENIDA (los primeros wps salen "normal" porque la vel~0 y el gear todavia esta en forward)
    # y el PRIMER MOVIMIENTO real es reversa, esos wps iniciales deben ser "reverse". Sino el wp0 (= m_LegStart
    # de un tramo aislado) queda "normal" -> ActiveLegIsReverse()=false y OrientBusToNext miran mal -> spawn
    # girado 180 y SIN hill-hold (Boris se cae por la pendiente al no clavar el handbrake). Solo toca el
    # ARRANQUE: si el primer movimiento es forward, corta en ese wp y no cambia nada.
    # El modo del arranque lo define el PRIMER MOVIMIENTO REAL, no el gear del parado (2026-07-26). En un corte
    # de secuencia Boris queda parado con el gear RESIDUAL del tramo anterior (ej. interc2: parado en gear 0
    # reversa, pero SEQ3 sale FORWARD) -> los frames parados salen "reverse" por el gear, y sin esto wp0 queda
    # reverse -> OrientBusToNext gira el spawn y ActiveLegIsReverse se confunde. Los parados iniciales heredan
    # el modo del primer wp que se MUEVE.
    first_move = None
    for i in range(len(WP)):
        if WP[i]["targetSpeed"] > 1.0:
            first_move = i
            break
    if first_move is not None and first_move > 0:
        mode0 = WP[first_move]["mode"]
        for j in range(first_move):
            WP[j]["mode"] = mode0

    # EL MODO DEL INTERCAMBIO LO DA EL TRAMO QUE TERMINA, NO EL AZAR DEL MUESTREO (2026-07-21, MEDIDO).
    # Estando detenido el humano pasa por NEUTRO: en las dos paradas de ESQ los frames muestran marchas
    # [0,1,2] mezcladas. Entonces el 'mode' que le toca al waypoint de la parada depende de cual frame
    # cayo en el resample -> el punto donde TERMINA la reversa salia 'normal'. Como ese wp es el ULTIMO
    # del tramo, su modo define con que marcha Boris LLEGA hasta el: tiene que ser el del tramo que cierra.
    for i, w in enumerate(WP):
        if w.get("legBreak") and i > 0:
            w["mode"] = WP[i - 1]["mode"]

    # saneo de retrocesos: mismo criterio que el motor, ver sanear_retrocesos ---
    keep, WP, retro_n, retro_peor = sanear_retrocesos(keep, WP)
    if retro_n:
        print("  saneo: %d wp descartados (peor retrocede %.1f m contra el rumbo grabado)" % (retro_n, retro_peor))

    # --- eventos desde is_marker=1 (UABZAIMark). BZMarkerEvent {wp, trigger, actions} ---
    # is_marker=2 NO es evento: es el corte de tramo, y va como legBreak en el waypoint (arriba).
    # el motor no conocia "waypoint" y estos eventos NUNCA disparaban; el tipo documentado es wp_reached.
    events = [{"wp": i, "trigger": {"type": "wp_reached", "wp": i}, "actions": []}
              for i, w in enumerate(keep) if w["mark"] == 1]
    # ★ --marca-wp: marcas AGREGADAS a mano, en indices del waypoint de salida.
    if args.marca_wp:
        ya = set(e["wp"] for e in events)
        for w in sorted(set(args.marca_wp)):
            if w < 0 or w >= len(keep):
                raise SystemExit("[ERROR] --marca-wp %d: la ruta tiene %d waypoints (0..%d)"
                                 % (w, len(keep), len(keep) - 1))
            if w in ya:
                print("[marcas] wp %d ya tenia marca, no se duplica" % w)
                continue
            events.append({"wp": w, "trigger": {"type": "wp_reached", "wp": w}, "actions": []})
            print("[marcas] AGREGADA marca en wp %d (%.1f, %.1f)"
                  % (w, keep[w]["x"], keep[w]["z"]))
        events.sort(key=lambda e: e["wp"])
    nbreak = sum(1 for w in WP if w.get("legBreak"))

    # --- fingerprint + header (mismo layout que bz_speedroute / el editor) ---
    ms    = float(hk["maxSteeringAngle"]); wb = float(hk["wheelbase"])
    mass  = float(hk.get("mass", 1000));   gears = int(hk.get("gearsCount", 6))
    rmin  = round(wb / math.tan(math.radians(ms)), 2)
    atts  = [a.strip() for a in hk.get("attachments", "").split(",") if a.strip()]
    fp = {"VehicleClass": hk["vehicleClass"], "MaxSteeringAngle": ms, "Wheelbase": round(wb, 3),
          "RminM": rmin, "Mass": mass, "GearsCount": gears}
    # MaxGear AUTO (2026-07-13): el gear MAS ALTO que uso el humano en la grabacion = el tope
    # correcto para ESTE vehiculo. Capea la seleccion del InverseModel -> no se ahoga subiendo a
    # gears que el vehiculo no necesita. Clave para modded: el Nissan_Red hace 82 km/h en 1ra
    # (gear 2) -> el humano nunca subio -> MaxGear=2. un jugador: "si en 1ra le da para la velocidad
    # limite de la toma que se quede ahi". Convencion DayZ: gear 2=1ra, 3=2da... (0=rev, 1=neutral).
    fwd_gears = [r["gear"] for r in rows if r["gear"] >= 2]
    max_gear = max(fwd_gears) if fwd_gears else 2
    hdr = {"RespawnDelay": 300, "AverageSpeedMS": 39, "VehicleClass": hk["vehicleClass"],
           "DriverClass": "eAI_SurvivorM_Boris", "Wheelbase": round(wb, 3), "Fingerprint": fp,
           "FollowPath": False, "Events": events, "Attachments": atts, "MaxGear": max_gear,
           # QUEST (, opcion (a)): el hook de OnQuestStart LEE estos dos
           # campos de los headers para auto-spawnear la ruta del quest -- y el
           # conversor no los emitia, asi que el modder tenia que grabar,
           # convertir y ABRIR EL JSON A MANO para escribir un campo que ninguna
           # documentacion menciona. Esa era la causa MECANICA de "deberia ser
           # mas facil interactuar con quest" (el usuario, 08/09), no una de redaccion.
           # -1 = desactivado: emitirlos en -1 no cambia ningun comportamiento,
           # solo hace VISIBLE la palanca.
           # ⚠ Es una medida PUENTE y reversible en una linea: cuando exista el
           # submodulo de misiones (la escena y el disparador viven en un
           # MANIFIESTO, no en el header de conduccion), el hook se muda alla y
           # estos dos campos se retiran de aca. Mientras el hook lea el header,
           # el header tiene que poder escribirse sin editor de texto.
           "QuestTravelID": -1, "QuestEscortID": -1}
    # GEAR STRATEGY DINAMICA por FISICA (2026-07-30): el SelectGear (banda RPM idle*1.3..redline*0.85) se ROMPE con
    # motores de banda ANGOSTA (diesel de bajas vueltas: Truck_01 redline 2400 -> redlineaba en 1ra y se clavaba).
    # Regla DERIVADA del vehiculo: redline bajo -> honrar los gears GRABADOS (la toma es la autoridad, per-vehiculo).
    # Los nafteros de altas vueltas (Sedan redline 5750) siguen en auto_box -> NO se tocan.
    _redline = float(hk.get("engineRPMRedline", 0) or 0)
    if 0 < _redline < 4000:
        hdr["GearStrategy"] = "follow_recording"
        print("  gear: redline %.0f (bajo) -> GearStrategy=follow_recording (honra gears grabados)" % _redline)
    # DECEL DEL ENDPOINT MEDIDA (2026-07-30): carry+slam del humano -> el iman la reproduce (frena desde velocidad
    # alta = freno fuerte, no el glide gentil que se auto-saboteaba). Per-vehiculo, del dato, no del config optimista.
    _dec = endpoint_brake_decel(rows)
    if _dec > 0:
        hdr["EndpointBrakeDecel"] = round(_dec, 2)
        print("  endpoint: decel medida %.2f m/s2 (carry+slam) -> EndpointBrakeDecel" % _dec)
    # EL MANUAL DE MANEJO (2026-07-22, RAIZ del "SEDAN corta el cordon"): sin la config del pipeline
    # el header sale PELADO -> Boris usa los defaults del codigo (UsePurePursuit=false, InverseModel=false,
    # PurePursuitCurveComp=0) -> corre STANLEY crudo, corta por dentro y se anticipa. Las tomas que
    # funcionaban (ESQ, FRAME01, NUEVO03) tenian estos ~70 flags. El template es la config buena conocida
    # (de ESQ pre-depuracion); la identidad del vehiculo (arriba) NO se pisa. Ver [[project_config_as_driving_manual]].
    _tmpl = os.path.join(os.path.dirname(os.path.abspath(__file__)), "driving_config_template.json")
    if os.path.exists(_tmpl):
        _cfg = json.load(open(_tmpl, encoding="utf-8"))
        for _k, _v in _cfg.items():
            if _k not in hdr:          # la identidad ya puesta arriba manda
                hdr[_k] = _v
    else:
        print("[WARN] falta driving_config_template.json -> el header sale sin config de manejo (Boris usa Stanley)")
    # EL CREEP VIAJA EN LA TOMA (ver creep_desde_envelope): del envelope de la clase que ya esta en el profile.
    _envp = os.path.join(args.profile, "envelope_%s.csv" % hk["vehicleClass"])
    _creep = creep_desde_envelope(_envp)
    if _creep:
        hdr["CreepKmh"], hdr["CreepSource"], hdr["CreepSamples"], hdr["CoastAtRest"] = _creep
        print("  creep: %.3f km/h (fuente %s, %d muestras, coast a v->0 %+.4f m/s2) -> viaja en el hdr" % _creep)
    else:
        print("  creep: sin envelope o sin coast2 para %s en %s -> el hdr no lo lleva; el motor lo deriva al cargar" % (hk["vehicleClass"], args.profile))
    # B6: EL ENVELOPE ENTERO VIAJA EN LA TOMA. Las lineas de envelope_<clase>.csv van tal cual en hdr["Envelope"] (el motor
    # las parsea con el mismo lector que el archivo, ANTES de mirar el archivo de la clase). Quien reciba la toma no necesita
    # el envelope aparte. Sin envelope en el profile, el hdr no lleva la clave y el motor sigue como hoy.
    _envl = envelope_lineas(_envp)
    if _envl:
        hdr["EnvelopeClass"] = hk["vehicleClass"]
        hdr["Envelope"] = _envl
        print("  envelope: %d lineas de %s -> viajan en el hdr (Envelope)" % (len(_envl), os.path.basename(_envp)))
    else:
        print("  envelope: sin envelope_%s.csv en %s -> el hdr no lo lleva; el motor usa el archivo de la clase o los priors" % (hk["vehicleClass"], args.profile))
    master = dict(hdr); master["Waypoints"] = WP

    # --- escribir json + hdr + wp.csv ---
    os.makedirs(args.profile, exist_ok=True)
    base = "BZBusRoute_" + args.route_name
    with open(os.path.join(args.profile, base + ".json"), "w", encoding="utf-8") as f:
        json.dump(master, f, indent=2)
    with open(os.path.join(args.profile, base + "_hdr.json"), "w", encoding="utf-8") as f:
        json.dump(hdr, f, indent=2)
    with open(os.path.join(args.profile, base + "_wp.csv"), "w", encoding="utf-8", newline="") as f:
        for w in WP:
            p = w["pos"]
            stop = 1 if w["isStop"] else 0
            dur = int(w.get("stopDuration", 0))
            brk = 1 if w.get("legBreak") else 0
            # 0-2 pos | 3 isStop | 4 stopDur | 5 stopRadius | 6 speed | 7 gear |
            # 8-11 thr/brk/hb/steer | 12 hasInputData=0 | 13 mode | 14 name(vacio) | 15 heading |
            # 16 lights | 17 horn | 18 frontWheel (angulo REAL de rueda -> lo lee BZVehicleEnvelope) |
            # 19 corridorHalfWidth (0 = usar la constante) | 20 legBreak (1 = INTERCAMBIO, corte de tramo)
            # 8 thr · 9 brk · 12 hasInputData. Sin --keep-inputs son ceros: exactamente la
            # fila que este conversor escribio siempre.
            hid = 1 if w["hasInputData"] else 0
            f.write("%s,%s,%s,%d,%d,0,%s,%d,%s,%s,0,0,%d,%s,,%s,0,0,%s,0,%d\n" %
                    (p[0], p[1], p[2], stop, dur, w["targetSpeed"], w["targetGear"],
                     w["targetThrottle"], w["targetBrake"], hid,
                     w["mode"], w["targetHeading"], w["targetFrontWheel"], brk))

    sp   = [w["targetSpeed"] for w in WP]
    nrev = sum(1 for w in WP if w["mode"] == "reverse")
    # el header DECLARA cuantas filas tienen el _wp.csv y el _cinta.csv. Es el denominador
    # del % de carga que muestra el panel; el conversor es el unico que lo sabe gratis (las acaba de
    # escribir) -- el server no puede saber el tamanio de un archivo sin leerlo dos veces.
    # Se reescribe el header DESPUES de la cinta porque recien ahi se sabe CintaRows.
    hdr["WpRows"] = len(WP)
    if args.cinta:
        n_cinta = emitir_cinta(args.frame, os.path.join(args.profile, base + "_cinta.csv"))
        if n_cinta:
            hdr["CintaRows"] = n_cinta
    with open(os.path.join(args.profile, base + "_hdr.json"), "w", encoding="utf-8") as f:
        json.dump(hdr, f, indent=2)
    print("RUTA '%s' -> %s" % (args.route_name, args.profile))
    print("  %d waypoints (de %d frames) | vel max %.0f avg %.0f km/h | reversa %d wps | eventos %d | intercambios %d | endpoint isStop=%s"
          % (len(WP), len(rows), max(sp), sum(sp) / len(sp), nrev, len(events), nbreak, WP[-1]["isStop"]))
    # --- TROMPO: solo aviso (ver detectar_trompos). La fila es la de la TOMA CRUDA, para poder cortarla. ---
    fila_cruda = {id(r): i for i, r in enumerate(rows)}
    for tr in detectar_trompos(filas_wp):
        ini = filas_wp[tr["desde"]]
        fc = fila_cruda[id(ini)]
        wp_cerca = len(keep) - 1
        for kw, rk in enumerate(keep):
            if fila_cruda.get(id(rk), -1) >= fc:
                wp_cerca = kw
                break
        print("  (!) TROMPO en la fila %d de la toma (t %.1f s | x %.1f z %.1f | ~wp %d): el chasis gira %.0f grados y desliza %.0f "
              "(%d filas) -- la toma NO es reproducible desde ahi: recorta la toma o regraba el final"
              % (fc, ini["t"], ini["x"], ini["z"], wp_cerca, tr["giro"], tr["desliz"], tr["filas_desliz"]))

if __name__ == "__main__":
    main()
