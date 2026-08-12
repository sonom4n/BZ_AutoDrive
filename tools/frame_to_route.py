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
                       # llego a 1.10m todavia convergiendo -> 6s para cerrar los ~2m. Sonom4n: "detenido 2 seg".

def resample(rows):
    """Muestrea la linea a arc-length adaptativo por velocidad. Conserva primero, ultimo y
    los frames con marca. Devuelve la sublista de rows elegidos (posiciones NATIVAS, no promediadas)."""
    out = [rows[0]]
    acc = 0.0
    prev = rows[0]
    for q in rows[1:]:
        step = math.hypot(q["x"] - prev["x"], q["z"] - prev["z"])
        prev = q
        # DETENIDO NO ACUMULA NI EMITE (2026-07-22, Sonom4n). Quedarse quieto para un cruce, armar una
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
        # se cruza sin parar (Sonom4n 2026-07-22: "me gustaria quedarme detenido en la grabacion, sea por
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
                    "hdg": float(r["heading"]),
                    # ANGULO REAL DE RUEDA (2026-07-20): el recorder lo mide con WheelGetDirection y el
                    # frame CSV lo trae, pero el conversor lo venia DESCARTANDO -> las rutas salian con
                    # col18=0 -> BZVehicleEnvelope no podia aprender el understeer k(v) del vehiculo y
                    # Boris viraba con el k=0.90 inventado. Medido en el M3: envelope con 0 muestras,
                    # se abria 1.9 m en la aproximacion y salia 64 deg torcido de la reversa.
                    "fw": float(r.get("front_wheel_deg", 0) or 0),
                    "brake": float(r.get("brake", 0) or 0),
                    "mark": int(float(r.get("is_marker", 0) or 0)),
                })
            except (ValueError, KeyError):
                pass
    return rows

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

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frame")
    ap.add_argument("route_name")
    ap.add_argument("--profile", default=r"C:\DayZServer2\profiles\BZ_AutoDrive",
                    help="dir de salida (profile del server que lee la ruta)")
    args = ap.parse_args()

    hk   = load_header(args.frame)
    rows = load_frames(args.frame)
    if len(rows) < 2:
        raise SystemExit("[ERROR] grabacion vacia o ilegible: " + args.frame)

    # --- resampleo a resolucion fisica (mata el micro-jitter que fingia curvatura -> zigzag) ---
    keep = resample(rows)

    # --- construir waypoints ---
    WP = []
    for i, w in enumerate(keep):
        prev = keep[i - 1] if i > 0 else None
        rev  = is_reverse(w, prev)
        WP.append({
            "pos": [round(w["x"], 3), round(w["y"], 3), round(w["z"], 3)],
            "isStop": False, "name": "", "stopDuration": 0, "stopRadius": 0.0,
            "targetSpeed": 0.0 if w.get("es_parada") else round(w["spd"], 2), "targetGear": int(w["gear"]),
            "targetThrottle": 0, "targetBrake": 0, "targetHandbrake": 0, "targetSteering": 0,
            "mode": "reverse" if rev else "normal", "hasInputData": False,
            "targetHeading": round(w["hdg"], 3),
            "targetFrontWheel": round(w.get("fw", 0.0), 3),
            # INTERCAMBIO (is_marker=2): corte de tramo DECLARADO por el autor (marcador manual; la vieja
            # tecla NUMPAD 3/UABZMarkLeg se saco 2026-08-11 -> hoy se AUTO-DETECTA del gear, ver abajo).
            # Boris trata ese wp como start point. No se infiere de un 0 km/h: un cero solo es una PAUSA.
            "legBreak": w["mark"] == 2,
        })

    # AUTO-INTERCAMBIO (2026-08-10, Sonom4n): el intercambio = cambio de SENTIDO (forward<->reverse), que
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
    # velocidad del tramo final (min con lo grabado) -> el resto del perfil queda intacto. Sonom4n lo
    # autorizo: "no importa fuerte o lento, pero clava el 0.00".
    # LOS INTERCAMBIOS (legBreak) SON ENDPOINTS (2026-07-26, Sonom4n: "llevar interc1 y 2 al nivel del endpoint,
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
            # DECEL EFECTIVA SEGUN PENDIENTE (2026-07-25, Sonom4n: "tiene que venir de abajo pisando el
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

    # ARRANQUE PARADO EN REVERSA (2026-07-25, Sonom4n: "Boris spawnea girado / se cae por la pendiente"). Si la
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

    # --- eventos desde is_marker=1 (UABZAIMark). BZMarkerEvent {wp, trigger, actions} ---
    # is_marker=2 NO es evento: es el corte de tramo, y va como legBreak en el waypoint (arriba).
    events = [{"wp": i, "trigger": {"type": "waypoint"}, "actions": []}
              for i, w in enumerate(keep) if w["mark"] == 1]
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
    # (gear 2) -> el humano nunca subio -> MaxGear=2. Sonom4n: "si en 1ra le da para la velocidad
    # limite de la toma que se quede ahi". Convencion DayZ: gear 2=1ra, 3=2da... (0=rev, 1=neutral).
    fwd_gears = [r["gear"] for r in rows if r["gear"] >= 2]
    max_gear = max(fwd_gears) if fwd_gears else 2
    hdr = {"RespawnDelay": 300, "AverageSpeedMS": 39, "VehicleClass": hk["vehicleClass"],
           "DriverClass": "eAI_SurvivorM_Boris", "Wheelbase": round(wb, 3), "Fingerprint": fp,
           "FollowPath": False, "Events": events, "Attachments": atts, "MaxGear": max_gear}
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
            f.write("%s,%s,%s,%d,%d,0,%s,%d,0,0,0,0,0,%s,,%s,0,0,%s,0,%d\n" %
                    (p[0], p[1], p[2], stop, dur, w["targetSpeed"], w["targetGear"],
                     w["mode"], w["targetHeading"], w["targetFrontWheel"], brk))

    sp   = [w["targetSpeed"] for w in WP]
    nrev = sum(1 for w in WP if w["mode"] == "reverse")
    print("RUTA '%s' -> %s" % (args.route_name, args.profile))
    print("  %d waypoints (de %d frames) | vel max %.0f avg %.0f km/h | reversa %d wps | eventos %d | intercambios %d | endpoint isStop=%s"
          % (len(WP), len(rows), max(sp), sum(sp) / len(sp), nrev, len(events), nbreak, WP[-1]["isStop"]))

if __name__ == "__main__":
    main()
