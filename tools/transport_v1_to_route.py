# -*- coding: utf-8 -*-
"""
transport_v1_to_route.py - IMPORTA una ruta de BrigadaZ Transport v1.0 a BZ_AutoDrive.

El caso: alguien uso BrigadaZ Transport v1, se grabo un recorrido largo en su server y no
quiere volver a grabarlo. No hace falta: la ruta de v1 NO vive en el PBO, vive en el profile
del server ($profile:BrigadaZ_Transport/BZBusRoute.json). Ese archivo es del usuario y alcanza
para reconstruir la ruta entera en el formato nuevo. Tampoco hace falta tener BrigadaZ Transport
instalado: se convierte el JSON, no el mod.

Escribe el trio que lee el server, igual que frame_to_route.py:
    BZBusRoute_<nombre>.json      master editable
    BZBusRoute_<nombre>_hdr.json  header sin waypoints
    BZBusRoute_<nombre>_wp.csv    21 columnas, leido por FGets

Uso:
    python transport_v1_to_route.py <ruta_v1.json> <nombre> --profile <carpeta de rutas>
                                    [--fingerprint <header_*.txt | _hdr.json>]

Que se mapea:
    El header de AutoDrive es superconjunto del de v1: RespawnDelay, AverageSpeedMS,
    VehicleClass, DriverClass, MaxGear y Attachments pasan tal cual. El resto del manual de
    manejo sale de driving_config_template.json (el mismo que usa frame_to_route.py).

    El waypoint tambien es superconjunto. targetHeading se DERIVA de la geometria
    (atan2(dx,dz), validado contra tomas nativas: error mediano 0.35 deg). steering /
    frontWheel / handbrake / luces / bocina / corridorHalfWidth / legBreak van en 0.

    Los pedales grabados por v1 se DESCARTAN (hasInputData=0): las tomas nativas de AutoDrive
    tampoco los usan. El control se reconstruye desde traza + velocidad grabada (pure-pursuit +
    modelo inverso), que es justo lo que la toma de v1 tiene.

Que se corrige de la toma v1:
    - puntos repetidos mientras el vehiculo estaba parado (se colapsan; el isStop se hereda).
    - paradas marcadas EN MOVIMIENTO: v1 marca donde el autor apreto la tecla, no donde el
      vehiculo estaba quieto. Se plancha el wp a 0 y se pinta la desaceleracion hacia atras.
    - el ultimo waypoint se marca como parada (AutoDrive lo espera para el endpoint).

Lo que NO puede salir de una toma v1: la identidad del vehiculo (Wheelbase / Fingerprint).
Eso no depende de la ruta sino del VEHICULO -> --fingerprint con cualquier header_*.txt que el
PathLogger escriba (una grabacion de 10 segundos alcanza) o el _hdr.json de una toma calibrada.
"""
import argparse, json, math, os, sys

AQUI = os.path.dirname(os.path.abspath(__file__))

# Campos del header de v1 que existen igual en AutoDrive.
DIRECTOS = ["RespawnDelay", "AverageSpeedMS", "VehicleClass", "DriverClass", "MaxGear", "Attachments"]


def norm180(a):
    while a > 180.0:
        a -= 360.0
    while a < -180.0:
        a += 360.0
    return a


def heading_geometrico(pts):
    """heading_deg = atan2(dx, dz), diferencia central. Si el tramo es degenerado arrastra el
    ultimo valido. Misma convencion que el heading que graba el PathLogger."""
    n = len(pts)
    out = [0.0] * n
    ultimo = 0.0
    for i in range(n):
        a = pts[max(0, i - 1)]
        b = pts[min(n - 1, i + 1)]
        dx, dz = b[0] - a[0], b[2] - a[2]
        if math.hypot(dx, dz) < 0.05:
            out[i] = ultimo
        else:
            ultimo = norm180(math.degrees(math.atan2(dx, dz)))
            out[i] = ultimo
    return out


def leer_identidad(ruta):
    """Identidad del vehiculo desde un header_*.txt del PathLogger o un _hdr.json calibrado.
    Misma cuenta que frame_to_route.py: Rmin = wheelbase / tan(maxSteeringAngle)."""
    if ruta.lower().endswith(".json"):
        with open(ruta, encoding="utf-8-sig") as fh:
            j = json.load(fh)
        return ({k: j[k] for k in ("VehicleClass", "Wheelbase", "Fingerprint", "Attachments",
                                   "GearStrategy", "EndpointBrakeDecel", "MaxGear") if k in j},
                "hdr calibrado")

    hk = {}
    for ln in open(ruta, encoding="utf-8"):
        if "=" in ln:
            k, v = ln.strip().split("=", 1)
            hk[k] = v
    ms = float(hk["maxSteeringAngle"])
    wb = float(hk["wheelbase"])
    ident = {
        "VehicleClass": hk["vehicleClass"],
        "Wheelbase": round(wb, 3),
        "Fingerprint": {
            "VehicleClass": hk["vehicleClass"],
            "MaxSteeringAngle": ms,
            "Wheelbase": round(wb, 3),
            "RminM": round(wb / math.tan(math.radians(ms)), 2),
            "Mass": float(hk.get("mass", 1000)),
            "GearsCount": int(hk.get("gearsCount", 6)),
        },
        "Attachments": [a.strip() for a in hk.get("attachments", "").split(",") if a.strip()],
    }
    # Motor de banda angosta (diesel de bajas vueltas): el SelectGear por banda RPM se rompe
    # -> honrar los gears grabados. Misma regla que frame_to_route.py.
    redline = float(hk.get("engineRPMRedline", 0) or 0)
    if 0 < redline < 4000:
        ident["GearStrategy"] = "follow_recording"
    return ident, "header del PathLogger"


def f(x, dec=3):
    """float -> string con punto decimal (nunca coma: el CSV se parte por coma)."""
    v = round(float(x), dec)
    return str(int(v)) if v == int(v) else repr(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ruta_v1", help="JSON de BrigadaZ Transport v1 (el del profile del server)")
    ap.add_argument("route_name")
    ap.add_argument("--profile", default=r"C:\DayZServer\profiles\BZ_AutoDrive",
                    help="carpeta de rutas del server (donde se deploya el trio)")
    ap.add_argument("--fingerprint", default=None,
                    help="identidad del vehiculo: header_*.txt del PathLogger o _hdr.json calibrado")
    ap.add_argument("--min-sep", type=float, default=0.05,
                    help="colapsa waypoints a menos de N metros del anterior")
    ap.add_argument("--rampa-freno", type=float, default=None,
                    help="m/s2 de la rampa de frenado a las paradas. Default: FollowPathBrakeDecel "
                         "del template. 0 = no tocar")
    ap.add_argument("--sin-endpoint", action="store_true", help="no marcar parada en el ultimo wp")
    ap.add_argument("--obstaculos", choices=["robusto", "interceptable", "ninguno"], default="ninguno",
                    help="perfil AR_OnWay (autos parados en el camino): robusto = frena + esquiva "
                         "(bus de linea 24/7), interceptable = frena y se queda (misiones), ninguno = off. "
                         "Requiere Modo 2/3 (UseInverseModel), que el template ya trae.")
    args = ap.parse_args()

    try:
        with open(args.ruta_v1, encoding="utf-8-sig") as fh:
            src = json.load(fh)
    except ValueError as e:
        sys.exit("[ERROR] no es un JSON valido: %s\n        %s" % (args.ruta_v1, e))
    if not isinstance(src, dict):
        sys.exit("[ERROR] el JSON no tiene forma de ruta (esperaba un objeto): " + args.ruta_v1)
    wps_in = src.get("Waypoints") or []
    if not wps_in:
        sys.exit("[ERROR] el JSON no tiene Waypoints. No parece una ruta de BrigadaZ Transport: "
                 + args.ruta_v1)
    if any(k in src for k in ("Fingerprint", "UsePurePursuit", "UseInverseModel")):
        print("[AVISO] este JSON ya parece una ruta de BZ_AutoDrive. Se importa igual, pero no "
              "hacia falta.")

    # EL MANUAL DE MANEJO: mismo template que frame_to_route.py. Sin esto el header sale pelado
    # y Boris cae en los defaults del codigo (Stanley crudo). Ver [[project_config_as_driving_manual]].
    tmpl = os.path.join(AQUI, "driving_config_template.json")
    if os.path.exists(tmpl):
        with open(tmpl, encoding="utf-8-sig") as fh:
            hdr = json.load(fh)
    else:
        print("[WARN] falta driving_config_template.json -> el header sale sin config de manejo")
        hdr = {}
    if args.rampa_freno is None:
        args.rampa_freno = float(hdr.get("FollowPathBrakeDecel", 3.0))

    # ---------------- header ----------------
    for k in DIRECTOS:
        if k in src:
            hdr[k] = src[k]
    hdr["FollowPath"] = False

    ident, origen_ident = ({}, None)
    if args.fingerprint:
        ident, origen_ident = leer_identidad(args.fingerprint)
        hdr.update(ident)
        # MaxGear: el gear mas alto que uso el humano (DayZ: 0=rev, 1=neutral, 2=1ra). frame_to_route
        # lo saca de los frames; aca de los targetGear de la toma v1.
        if "MaxGear" not in ident:
            gears = [int(w.get("targetGear", 0) or 0) for w in wps_in]
            gears = [g for g in gears if g >= 2]
            if gears:
                hdr["MaxGear"] = max(gears)

    # Defaults explicitos: la ruta de otro server puede venir sin estos campos (repack, o el
    # JsonFileLoader los omitio). Enforce usaria el default de la clase igual, pero un _hdr
    # explicito se lee y se edita mejor.
    hdr.setdefault("DriverClass", "eAI_SurvivorM_Boris")
    hdr.setdefault("RespawnDelay", 300)
    hdr.setdefault("AverageSpeedMS", 11.0)
    hdr.setdefault("Events", [])
    hdr.setdefault("Crew", [])

    # Perfil de obstaculos (AR_OnWay). El tick lo gatea con (ObstacleSlow || ObstacleEscape) &&
    # UseInverseModel -> el template ya trae UseInverseModel=true, asi que esto engancha. Un bus de
    # linea 24/7 suele querer 'robusto' (sortea un auto parado en el camino). Ver manual A.5b.
    _obs = {"robusto": (True, True), "interceptable": (True, False), "ninguno": (False, False)}
    hdr["ObstacleSlow"], hdr["ObstacleEscape"] = _obs[args.obstaculos]

    # ---------------- waypoints ----------------
    limpios, colapsados = [], 0
    for w in wps_in:
        p = w.get("pos") or [0, 0, 0]
        if len(p) < 3:
            continue
        if limpios:
            q = limpios[-1]["pos"]
            if math.hypot(p[0] - q[0], p[2] - q[2]) < args.min_sep:
                colapsados += 1
                # el punto se descarta, pero si traia la parada la hereda el que quedo
                if w.get("isStop") and not limpios[-1].get("isStop"):
                    limpios[-1]["isStop"] = True
                    limpios[-1]["name"] = w.get("name", "")
                    limpios[-1]["stopDuration"] = w.get("stopDuration", 0)
                    limpios[-1]["stopRadius"] = w.get("stopRadius", 0)
                continue
        limpios.append(dict(w))

    if not limpios:
        sys.exit("[ERROR] no quedo ningun waypoint valido (todos sin 'pos'?).")

    if not args.sin_endpoint:
        limpios[-1]["isStop"] = True
        if not limpios[-1].get("stopDuration"):
            limpios[-1]["stopDuration"] = 6

    hdgs = heading_geometrico([w["pos"] for w in limpios])
    WP = []
    for i, w in enumerate(limpios):
        p = w["pos"]
        WP.append({
            "pos": [round(float(p[0]), 3), round(float(p[1]), 3), round(float(p[2]), 3)],
            "isStop": bool(w.get("isStop", False)),
            "name": str(w.get("name", "")).replace(",", " "),
            "stopDuration": int(w.get("stopDuration", 0) or 0),
            "stopRadius": float(w.get("stopRadius", 0) or 0),
            "targetSpeed": round(float(w.get("targetSpeed", 0) or 0), 2),
            "targetGear": int(w.get("targetGear", 0) or 0),
            "targetThrottle": 0.0, "targetBrake": 0.0, "targetHandbrake": 0.0,
            "targetSteering": 0.0, "hasInputData": False, "mode": "normal",
            "targetHeading": round(hdgs[i], 3),
            "targetLights": 0, "targetHorn": 0, "targetFrontWheel": 0.0,
            "corridorHalfWidth": 0.0, "legBreak": False,
        })

    # ---------------- paradas: planchar a 0 + rampa de frenado ----------------
    # v1 marca la parada donde el autor apreto la tecla, no donde el vehiculo estaba quieto: hay
    # paradas declaradas a 15+ km/h (ya arrancando). AutoDrive cruzaria esa velocidad como target
    # -> pasa de largo. Se plancha a 0 y se pinta v <= sqrt(2*a*d) hacia atras, sin SUBIR nunca
    # una velocidad grabada.
    parches = []
    if args.rampa_freno > 0:
        for i, w in enumerate(WP):
            if not w["isStop"]:
                continue
            v_orig = w["targetSpeed"]
            w["targetSpeed"] = 0.0
            d, j, tocados = 0.0, i - 1, 0
            while j >= 0:
                d += math.hypot(WP[j]["pos"][0] - WP[j + 1]["pos"][0],
                                WP[j]["pos"][2] - WP[j + 1]["pos"][2])
                cap = math.sqrt(2.0 * args.rampa_freno * d) * 3.6
                if WP[j]["targetSpeed"] <= cap:
                    break                     # la grabacion ya frena mas fuerte: no se toca
                WP[j]["targetSpeed"] = round(cap, 2)
                tocados += 1
                j -= 1
            if tocados or v_orig > 1.0:
                parches.append((w["name"] or ("wp" + str(i)), v_orig, tocados, d))

    # ---------------- escribir json + hdr + wp.csv ----------------
    os.makedirs(args.profile, exist_ok=True)
    base = "BZBusRoute_" + args.route_name
    master = dict(hdr)
    master["Waypoints"] = WP
    with open(os.path.join(args.profile, base + ".json"), "w", encoding="utf-8") as fh:
        json.dump(master, fh, indent=2, ensure_ascii=False)
    with open(os.path.join(args.profile, base + "_hdr.json"), "w", encoding="utf-8") as fh:
        json.dump(hdr, fh, indent=2, ensure_ascii=False)
    with open(os.path.join(args.profile, base + "_wp.csv"), "w", encoding="utf-8", newline="") as fh:
        for w in WP:
            fh.write(",".join([
                f(w["pos"][0]), f(w["pos"][1]), f(w["pos"][2]),
                "1" if w["isStop"] else "0", str(w["stopDuration"]), f(w["stopRadius"]),
                f(w["targetSpeed"], 2), str(w["targetGear"]),
                "0", "0", "0", "0", "0", w["mode"], w["name"],
                f(w["targetHeading"]), "0", "0", "0", "0", "0",
            ]) + "\n")

    # ---------------- reporte ----------------
    ds = [math.hypot(b["pos"][0] - a["pos"][0], b["pos"][2] - a["pos"][2])
          for a, b in zip(WP, WP[1:])]
    stops = [w for w in WP if w["isStop"]]
    sp = [w["targetSpeed"] for w in WP]
    print("RUTA '%s' -> %s" % (args.route_name, args.profile))
    print("  %d waypoints (de %d en la toma v1, %d colapsados) | %.0f m | %d paradas | vel max %.0f km/h"
          % (len(WP), len(wps_in), colapsados, sum(ds), len(stops), max(sp)))
    print("  heading derivado de la geometria | pedales v1 descartados (hasInputData=0)")
    print("  perfil obstaculos: %s (ObstacleSlow=%s ObstacleEscape=%s)"
          % (args.obstaculos, hdr["ObstacleSlow"], hdr["ObstacleEscape"]))
    if parches:
        print("  paradas corregidas (v1 las marca en movimiento):")
        for nom, v0, n, dist in parches:
            print("    %-18s declarada a %6.2f km/h -> 0, %d wps repintados (%.0f m)" % (nom, v0, n, dist))
    if origen_ident:
        fp = hdr.get("Fingerprint", {})
        print("  identidad: %s | Wheelbase=%s Rmin=%s m Mass=%s gears=%s MaxGear=%s"
              % (origen_ident, hdr.get("Wheelbase"), fp.get("RminM"), fp.get("Mass"),
                 fp.get("GearsCount"), hdr.get("MaxGear")))
        if "EndpointBrakeDecel" not in ident:
            print("  OJO: EndpointBrakeDecel no esta MEDIDO para este vehiculo (el header_*.txt no lo"
                  " trae). Se mide del frenazo grabado: alcanza una grabacion corta llegando y frenando.")
    else:
        print("  SIN identidad de vehiculo: la ruta anda, pero sin Wheelbase/Fingerprint el control"
              " no es fiel. Volve a importar con --fingerprint.")


if __name__ == "__main__":
    main()
