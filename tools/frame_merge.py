# -*- coding: utf-8 -*-
"""
frame_merge.py -- MERGE de tramos de grabacion.

La feature "continuar grabacion" deja una ruta larga partida en varios frame_*.csv.
Esto los une en UN frame mergeado, listo para frame_to_route + envelope_emit:

  1. El ORDEN sale del EMPALME POR POSICION (fin de un tramo ~ inicio del siguiente),
     NUNCA del timestamp (entre sesiones hay huecos y relojes distintos) ni del nombre.
  2. DUPLICADOS (mismo inicio Y mismo fin que otro tramo): se queda UNO (el primero
     dado) y se avisa cual se descarto.
  3. La fila de empalme (inicio del tramo siguiente duplicando el fin del anterior)
     se salta; los micro-saltos restantes los limpia el saneo de retrocesos aguas abajo.
  4. t_cum se re-acumula (frame_to_route lo ignora, pero envelope_emit mide dt entre
     filas: sin re-acumular, el par del empalme daria dt negativo).
  5. Los is_marker de CADA tramo viajan intactos (las filas van verbatim; solo se
     reescribe la columna t_cum).
  6. Header apareado: se copia el del primer tramo. Si los tramos son de VEHICULOS
     DISTINTOS, se ABORTA (un merge de dos autos no es una ruta, es un error).

Uso:
    python frame_merge.py <nombre_salida> <frame1.csv> <frame2.csv> [...] [--out DIR]
Los tramos se pasan EN CUALQUIER ORDEN: el orden real lo decide el empalme.
"""
import io
import math
import os
import sys

CHAIN_TOL_M = 8.0    # fin->inicio a menos de esto = empalman (continuar spawnea donde paraste)
DUP_TOL_M = 5.0      # inicio Y fin a menos de esto de otro tramo = duplicado
SEAM_SKIP_M = 0.5    # la fila de empalme se salta si duplica el fin anterior a menos de esto


def leer(path):
    f = io.open(path, encoding="utf-8", errors="replace")
    cab = f.readline().strip()
    cols = [c.strip() for c in cab.split(",")]
    idx = {}
    for i, c in enumerate(cols):
        idx[c] = i
    if "x" not in idx or "z" not in idx or "t_cum" not in idx:
        raise SystemExit("[merge] %s: sin columnas x/z/t_cum -- no es un frame CSV" % os.path.basename(path))
    filas = []
    for ln in f:
        ln = ln.rstrip("\n").rstrip("\r")
        if ln == "":
            continue
        p = ln.split(",")
        if len(p) < len(cols):
            continue
        filas.append(p)
    f.close()
    if len(filas) < 2:
        raise SystemExit("[merge] %s: vacio o ilegible" % os.path.basename(path))
    hdr_path = path.replace("frame_", "header_").rsplit(".", 1)[0] + ".txt"
    veh = ""
    if os.path.isfile(hdr_path):
        for hl in io.open(hdr_path, encoding="utf-8"):
            if hl.startswith("vehicleClass="):
                veh = hl.strip().split("=", 1)[1]
                break
    return {"path": path, "cab": cab, "idx": idx, "filas": filas, "veh": veh, "hdr": hdr_path}


def pxz(t, fila):
    return (float(fila[t["idx"]["x"]]), float(fila[t["idx"]["z"]]))


def d2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def encadenar(tramos):
    """orden por empalme de posicion. Devuelve la lista ordenada o aborta EXPLICITO."""
    # 1. duplicados fuera
    unicos = []
    for t in tramos:
        dup = None
        for u in unicos:
            if (d2(pxz(t, t["filas"][0]), pxz(u, u["filas"][0])) < DUP_TOL_M
                    and d2(pxz(t, t["filas"][-1]), pxz(u, u["filas"][-1])) < DUP_TOL_M):
                dup = u
                break
        if dup:
            print("[merge] DUPLICADO descartado: %s (mismo inicio+fin que %s)"
                  % (os.path.basename(t["path"]), os.path.basename(dup["path"])))
        else:
            unicos.append(t)
    if len(unicos) < 2:
        raise SystemExit("[merge] tras descartar duplicados queda %d tramo: nada que mergear" % len(unicos))
    # 2. vehiculo unico
    vehs = sorted(set(t["veh"] for t in unicos if t["veh"]))
    if len(vehs) > 1:
        raise SystemExit("[merge] ABORTA: tramos de vehiculos DISTINTOS (%s). Un merge de dos autos no es una ruta." % ", ".join(vehs))
    # 3. el arranque: su inicio no empalma con el fin de NADIE
    inicios = []
    for t in unicos:
        es_inicio = True
        for u in unicos:
            if u is t:
                continue
            if d2(pxz(t, t["filas"][0]), pxz(u, u["filas"][-1])) < CHAIN_TOL_M:
                es_inicio = False
                break
        if es_inicio:
            inicios.append(t)
    if len(inicios) != 1:
        nombres = ", ".join(os.path.basename(t["path"]) for t in inicios)
        raise SystemExit("[merge] ABORTA: %d candidatos a tramo INICIAL (%s). La cadena tiene que ser univoca -- no se adivina." % (len(inicios), nombres))
    # 4. seguir la cadena fin -> inicio
    orden = [inicios[0]]
    resto = [t for t in unicos if t is not inicios[0]]
    while resto:
        fin = pxz(orden[-1], orden[-1]["filas"][-1])
        prox = [t for t in resto if d2(pxz(t, t["filas"][0]), fin) < CHAIN_TOL_M]
        if len(prox) != 1:
            nombres = ", ".join(os.path.basename(t["path"]) for t in prox)
            raise SystemExit("[merge] ABORTA: desde el fin de %s empalman %d tramos (%s). Cadena ambigua o cortada."
                             % (os.path.basename(orden[-1]["path"]), len(prox), nombres))
        orden.append(prox[0])
        resto.remove(prox[0])
    return orden


def mergear(nombre, paths, out_dir):
    tramos = [leer(p) for p in paths]
    cab0 = tramos[0]["cab"]
    for t in tramos[1:]:
        if t["cab"] != cab0:
            raise SystemExit("[merge] ABORTA: %s tiene OTRAS columnas que el primero" % os.path.basename(t["path"]))
    orden = encadenar(tramos)

    it = orden[0]["idx"]["t_cum"]
    out = []
    t_base = 0.0
    saltadas = 0
    marca_col = orden[0]["idx"].get("is_marker", -1)
    markers = 0
    for k, t in enumerate(orden):
        filas = t["filas"]
        if k > 0:
            fin_prev = pxz(orden[k - 1], orden[k - 1]["filas"][-1])
            if d2(pxz(t, filas[0]), fin_prev) < SEAM_SKIP_M:
                filas = filas[1:]
                saltadas += 1
            gap = d2(pxz(t, filas[0]), fin_prev)
            print("[merge] empalme %d: %s -> %s (%.2f m)" % (k, os.path.basename(orden[k - 1]["path"]),
                                                             os.path.basename(t["path"]), gap))
        t0 = float(filas[0][it])
        for fila in filas:
            f2 = list(fila)
            f2[it] = "%.6g" % (t_base + (float(fila[it]) - t0))
            if marca_col >= 0 and f2[marca_col].strip() not in ("", "0"):
                markers += 1
            out.append(",".join(f2))
        t_base = float(out[-1].split(",")[it]) + 0.025

    fp = os.path.join(out_dir, "frame_%s.csv" % nombre)
    hp = os.path.join(out_dir, "header_%s.txt" % nombre)
    io.open(fp, "w", encoding="utf-8", newline="\n").write(cab0 + "\n" + "\n".join(out) + "\n")
    io.open(hp, "w", encoding="utf-8", newline="\n").write(io.open(orden[0]["hdr"], encoding="utf-8").read())
    print("[merge] %s: %d tramos -> %d filas (%d empalmes saltados) · %d markers preservados · vehiculo %s"
          % (os.path.basename(fp), len(orden), len(out), saltadas, markers, orden[0]["veh"]))
    print("[merge] orden final: " + " -> ".join(os.path.basename(t["path"]) for t in orden))
    return fp


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    nombre = argv[1]
    paths = []
    out_dir = None
    i = 2
    while i < len(argv):
        if argv[i] == "--out":
            out_dir = argv[i + 1]
            i += 2
        else:
            paths.append(argv[i])
            i += 1
    for p in paths:
        if not os.path.isfile(p):
            print("[merge] no existe: " + p)
            return 2
    if not out_dir:
        out_dir = os.path.dirname(os.path.abspath(paths[0]))
    mergear(nombre, paths, out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
