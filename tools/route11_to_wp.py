# -*- coding: utf-8 -*-
"""
route11_to_wp.py -- exporter FIEL: master .json de 1.1 -> el triple que 1.2 necesita.

El caso: una ruta 1.1 heredada/shipeada es.json-only.
Bajo el motor 1.2 swapeado, sin `_wp.csv` NO maneja (BZRouteLoader no tiene fallback a
JSON). Este exporter escribe el `_wp.csv` (y el `_hdr.json` si falta) DESDE el master,
columna por columna, SIN resamplear ni reprocesar: la ruta ya son waypoints, no frames.

FIEL de verdad (a diferencia de transport_v1_to_route.py, que deriva rumbo GEOMETRICO
y pisa mode con "normal"):
  - col15 = targetHeading GRABADO del waypoint (0 = "sin dato", convencion del formato:
    el motor cae a tangente y lo dice). NUNCA se inventa un rumbo.
  - col13 = mode tal cual (reverse/parking se preservan).
  - col20 = legBreak declarado, tal cual. SIN auto-marcar bordes de sentido: el master
    YA los trae (frame_to_route los marca antes de escribir) -- la paridad de oro atrapo
    que re-derivarlos agregaba legBreaks que el _wp.csv real no tiene.
  - Events[] no viajan al _wp.csv (viven en el json/hdr, los consume el runner).

Columnas (autoridad: BZRouteLoader.AddLine + el escritor de frame_to_route.py):
  0-2 pos | 3 isStop | 4 stopDur | 5 stopRadius | 6 speed | 7 gear | 8 thr | 9 brk |
  10 handbrake | 11 steering | 12 hasInputData | 13 mode | 14 name | 15 heading |
  16 lights | 17 horn | 18 frontWheel | 19 corridorHalfWidth | 20 legBreak

Uso:
    python route11_to_wp.py <BZBusRoute_X.json> [--out DIR] [--pisar]
No pisa un _wp.csv existente salvo --pisar (mismo contrato que envelope_emit).
"""
import io
import json
import os
import sys


def num(w, k, d=0):
    v = w.get(k, d)
    if v is None:
        return d
    return v


def f(x):
    if isinstance(x, bool):
        return "1" if x else "0"
    if isinstance(x, int):
        return str(x)
    return ("%.6g" % float(x))


def exportar(src, out_dir, pisar):
    d = json.load(io.open(src, encoding="utf-8"))
    wps = d.get("Waypoints")
    if not wps:
        print("[route11] %s: sin Waypoints[] -- no es un master de ruta" % os.path.basename(src))
        return 1
    base = os.path.basename(src)[:-5]          # sin ".json"
    wp_path = os.path.join(out_dir, base + "_wp.csv")
    hdr_path = os.path.join(out_dir, base + "_hdr.json")
    if os.path.exists(wp_path) and not pisar:
        print("[route11] ya existe %s: NO se pisa (--pisar para regenerar)" % os.path.basename(wp_path))
        return 0

    lineas = []
    con_rumbo = 0
    nrev = 0
    nbrk = 0
    for i, w in enumerate(wps):
        p = w.get("pos", [0, 0, 0])
        mode = (w.get("mode") or "normal")
        hdg = num(w, "targetHeading", 0)
        if hdg:
            con_rumbo += 1
        if mode == "reverse":
            nrev += 1
        # legBreak: SOLO el declarado del wp. NO se auto-marcan bordes de sentido: el
        # frame_to_route los marca ANTES de escribir el master, asi que el json YA los trae
        # donde corresponden -- re-derivarlos aca agregaba legBreaks que el _wp.csv real no
        # tiene (lo atrapo la paridad de oro en 3 rutas). Fidelidad = copiar, no opinar.
        brk = bool(w.get("legBreak"))
        if brk:
            nbrk += 1
        lineas.append([
            f(p[0]), f(p[1]), f(p[2]),
            "1" if w.get("isStop") else "0",
            f(int(num(w, "stopDuration", 0))),
            f(num(w, "stopRadius", 0)),
            f(num(w, "targetSpeed", 0)),
            f(int(num(w, "targetGear", 2))),
            f(num(w, "targetThrottle", 0)),
            f(num(w, "targetBrake", 0)),
            f(num(w, "targetHandbrake", 0)),
            f(num(w, "targetSteering", 0)),
            "1" if w.get("hasInputData") else "0",
            mode,
            w.get("name", "") or "",
            f(hdg),
            f(num(w, "targetLights", 0)),
            f(num(w, "targetHorn", 0)),
            f(num(w, "targetFrontWheel", 0)),
            f(num(w, "corridorHalfWidth", 0)),
            "1" if brk else "0",
        ])
    io.open(wp_path, "w", encoding="utf-8", newline="\n").write(
        "\n".join(",".join(l) for l in lineas) + "\n")

    hecho_hdr = ""
    if not os.path.exists(hdr_path):
        hdr = dict(d)
        hdr.pop("Waypoints", None)
        io.open(hdr_path, "w", encoding="utf-8", newline="\n").write(
            json.dumps(hdr, indent=2, ensure_ascii=False))
        hecho_hdr = " + _hdr.json"

    aviso = ""
    if con_rumbo == 0:
        aviso = "  !! SIN rumbo grabado (col15=0 en todos): el motor cae a tangente y lo avisa"
    print("[route11] %s -> %d wps%s | rumbo %d/%d | reversa %d | legBreak %d%s"
          % (base, len(lineas), hecho_hdr, con_rumbo, len(lineas), nrev, nbrk, aviso))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    src = argv[1]
    out_dir = os.path.dirname(os.path.abspath(src))
    pisar = "--pisar" in argv
    i = 2
    while i < len(argv):
        if argv[i] == "--out":
            out_dir = argv[i + 1]
            i += 2
        else:
            i += 1
    if not os.path.isfile(src):
        print("[route11] no existe: " + src)
        return 2
    return exportar(src, out_dir, pisar)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
