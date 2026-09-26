# -*- coding: utf-8 -*-
"""Prototipo: derivar el ENVELOPE de una toma grabada (frame_*.csv), sin corrida de banco.

Idea del usuario: el frame_*.csv ya trae `steering` (comando), `front_wheel_deg`
(resultado), `throttle`, `brake`, `speed_kmh`, `heading` y `x,y,z`. La calibracion mide
exactamente eso -- pero en una corrida aparte, por vehiculo.

! LA REGLA DE ESTE PROTOTIPO: se usa el MISMO criterio que el banco, constante por constante,
sacadas de BZEnvelopeRecorder. No se afloja ninguna para que salgan mas muestras. Si con el
criterio del banco la toma no alcanza, la respuesta es que no alcanza -- y eso tambien es un
resultado. La cicatriz del steerTransfer (una transferencia mal medida empeoro el cross-track
4.5x) prohibe explicitamente relajar el filtro para conseguir cobertura.
"""
import io, math, sys

# ! salida ASCII a proposito: la consola de Windows es cp1252 y los simbolos rompian el
# script al imprimir. Un instrumento que crashea al reportar no es un instrumento.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except AttributeError:
    pass

# ---- constantes, copiadas de BZEnvelopeRecorder / BZEnvelopeStore ----
K_MIN_MS = 3.0                 # transferencia: sin velocidad no hay respuesta que medir
STX_EPS_CMD = 0.01             # comando quieto
STX_EPS_ANG = 0.005            # rad (~0.3 grados): la rueda llego
STX_ESTABLE_TICKS = 3          # dos senales quietas, no un tiempo a ojo
ST_BUCKETS = 10

C_MAX_THROTTLE = 0.02
C_MAX_BRAKE = 0.02
C_MAX_HB = 0.02
C_MAX_STEER = 0.15             # rad: volante cruzado = arrastre, no planeo
C_MIN_MS = 0.3                 # el piso ya bajado en
C_MAX_PITCH = 0.0383           # 2.2 grados
C_MIN_DECEL = 0.02
C_MAX_DECEL = 12.0
BINW = 5.0
NBINS = 20


COLS = ("t_cum", "dt", "throttle", "brake", "steering", "gear", "handbrake",
        "x", "y", "z", "heading", "speed_kmh", "rpm", "front_wheel_deg",
        # ---- columnas OPCIONALES (tomas nuevas). Ausentes -> se marcan como faltantes y
        # el analisis cae al derivado, sin inventar. Ver el veredicto.
        "pitch_deg", "surface", "chassis_kmh")
FALTA = -999999.0
PRESENTES = {}

# ⚠⚠⚠ COLUMNAS DE TEXTO. `surface` vale "asphalt_ext", no un numero, y `leer()` exigia que
# TODAS las columnas de COLS parsearan a float: la primera que fallaba marcaba la fila como
# mala y la descartaba ENTERA. Resultado medido sobre la toma del jesko (7.749 filas):
#   con 'surface' en COLS -> 0 filas utiles (0%)   ·   sin ella -> 7.749 (100%)
# Lo mismo en hx58, m4_tractor_winter y AEC_Matador. O sea: el tool que el wizard llama
# para emitir el envelope no podia emitir NINGUNO desde una toma moderna.
#
# Se declaran explicitamente en vez de "cualquier cosa que no parsee": asi una columna
# NUMERICA con basura sigue descartando la fila, que es para lo que existia la guarda.
# El valor se codifica a un entero estable (categorico) porque la fila es una tupla de
# floats -- pero OJO: `SURF` no lo lee nadie, solo se reporta su PRESENCIA.
COLS_TEXTO = ("surface",)
CATEGORIAS = {}    # columna -> {texto: codigo}
DESCARTES = {}     # columna -> filas descartadas por no parsear


def _codigo(col, txt):
    """Codigo estable por valor de texto. No inventa un cero: inventa un INDICE, y la
    tabla queda en CATEGORIAS para poder imprimirla."""
    tabla = CATEGORIAS.setdefault(col, {})
    if txt not in tabla:
        tabla[txt] = len(tabla)
    return float(tabla[txt])


def exigir_filas(fil, path):
    """★ LA GUARDA QUE FALTABA. Sin esto el fallo era SILENCIOSO hacia arriba: `leer`
    devolvia [], `escribir` igual generaba el archivo (scalars + cero bins = el STUB de
    20 bytes) y RECIEN DESPUES reventaba con IndexError al imprimir el resumen. O sea que
    el tool dejaba un envelope envenenado en disco y el wizard seguia como si nada.
    Un instrumento que no puede medir tiene que DECIRLO y salir distinto de 0."""
    if fil:
        return
    print("")
    print("✖ CERO FILAS UTILES en %s" % path)
    if DESCARTES:
        peor = sorted(DESCARTES.items(), key=lambda kv: -kv[1])
        print("  filas descartadas por columna (no parsea a numero):")
        for c, n in peor[:6]:
            print("    %-16s %d" % (c, n))
        print("  -> si esa columna es de TEXTO, agregala a COLS_TEXTO.")
    else:
        print("  el archivo no tiene filas de datos.")
    print("  NO se emite envelope: un envelope vacio es peor que ninguno (cae al prior de caja).")
    sys.exit(2)


def leer(path):
    """Lee por NOMBRE de columna. Las tomas viejas no traen las opcionales: se detecta y se
    informa, no se rellena con cero (un cero silencioso es un dato inventado)."""
    f = io.open(path, encoding="utf-8", errors="replace")
    cab = [c.strip() for c in f.readline().strip().split(",")]
    idx = {}
    for i, c in enumerate(cab):
        idx[c] = i
    PRESENTES.clear()
    for c in COLS:
        PRESENTES[c] = c in idx
    CATEGORIAS.clear()
    DESCARTES.clear()
    filas = []
    for ln in f:
        p = ln.strip().split(",")
        fila = []
        malo = False
        for c in COLS:
            if c not in idx or idx[c] >= len(p):
                fila.append(FALTA)
                continue
            bruto = p[idx[c]].strip()
            if c in COLS_TEXTO:
                fila.append(_codigo(c, bruto))
                continue
            try:
                fila.append(float(bruto))
            except ValueError:
                # columna NUMERICA con basura: la fila se descarta, como siempre -- pero
                # ahora queda contada y con nombre, para que el diagnostico sea posible.
                DESCARTES[c] = DESCARTES.get(c, 0) + 1
                malo = True
                break
        if not malo:
            filas.append(fila)
    f.close()
    return filas


T, DT, THR, BRK, STE, GEAR, HB, X, Y, Z, HEAD, KMH, RPM, FWD, PITCH, SURF, CHAS = range(17)


def analizar(path):
    fil = leer(path)
    exigir_filas(fil, path)
    print("toma: %s" % path.split("/")[-1].split("\\")[-1])
    print("  %d filas . %.1f s . vel %.1f a %.1f km/h"
          % (len(fil), fil[-1][T] - fil[0][T],
             min(r[KMH] for r in fil), max(r[KMH] for r in fil)))

    # ---------------- steerTransfer, criterio del banco ----------------
    print("")
    print("=== steerTransfer (comando -> angulo), criterio del banco ===")
    baldes = {}
    estable = 0
    cmd_prev = None
    ang_prev = None
    rech = {"lento": 0, "tope": 0, "transicion": 0}
    for r in fil:
        cmd = abs(r[STE])
        ang = abs(math.radians(r[FWD]))
        ms = (r[CHAS] if r[CHAS] != FALTA else r[KMH]) / 3.6
        if ang >= 1.5708:
            rech["tope"] += 1
            cmd_prev, ang_prev = cmd, ang
            continue
        if ms < K_MIN_MS:
            rech["lento"] += 1
            cmd_prev, ang_prev = cmd, ang
            estable = 0
            continue
        if cmd_prev is None:
            cmd_prev, ang_prev = cmd, ang
            continue
        quieto = (abs(cmd - cmd_prev) < STX_EPS_CMD and abs(ang - ang_prev) < STX_EPS_ANG)
        cmd_prev, ang_prev = cmd, ang
        if not quieto:
            estable = 0
            rech["transicion"] += 1
            continue
        estable += 1
        if estable < STX_ESTABLE_TICKS:
            continue
        b = min(ST_BUCKETS - 1, max(0, int(cmd * ST_BUCKETS)))
        s, n, mx = baldes.get(b, (0.0, 0, 0.0))
        baldes[b] = (s + ang, n + 1, max(mx, ang))
    print("  rechazos: %d por lento (<%.0f m/s) . %d por transicion . %d por tope"
          % (rech["lento"], K_MIN_MS, rech["transicion"], rech["tope"]))
    print("  balde  rango cmd     n    angulo medio      lineal si fuera proporcional")
    smax = max((v[2] for v in baldes.values()), default=0.0)
    pobl = 0
    monot = True
    ant = -1.0
    for b in range(ST_BUCKETS):
        if b not in baldes:
            print("    %d    %.1f-%.1f      --   (sin dato -> fallback)" % (b, b / 10.0, (b + 1) / 10.0))
            continue
        s, n, mx = baldes[b]
        med = s / n
        pobl += 1
        lin = smax * ((b + 0.5) / ST_BUCKETS)
        print("    %d    %.1f-%.1f    %4d    %.4f rad (%5.2f deg)   %.4f"
              % (b, b / 10.0, (b + 1) / 10.0, n, med, math.degrees(med), lin))
        if med < ant - 1e-9:
            monot = False
        ant = med
    print("  -> %d de %d baldes poblados . monotona: %s" % (pobl, ST_BUCKETS, "SI" if monot else "NO"))

    # ---------------- coastDecel, criterio del banco ----------------
    print("")
    print("=== coastDecel (planeo libre), criterio del banco ===")
    bins = {}
    rech2 = {"gas": 0, "freno": 0, "hb": 0, "volante": 0, "lento": 0, "pitch": 0, "rango": 0}
    for i in range(1, len(fil)):
        a, r = fil[i - 1], fil[i]
        ms = (r[CHAS] if r[CHAS] != FALTA else r[KMH]) / 3.6
        dt = r[DT]
        if dt <= 0.001:
            continue
        if a[THR] > C_MAX_THROTTLE:
            rech2["gas"] += 1; continue
        if a[BRK] > C_MAX_BRAKE:
            rech2["freno"] += 1; continue
        if a[HB] > C_MAX_HB:
            rech2["hb"] += 1; continue
        if abs(math.radians(a[FWD])) > C_MAX_STEER:
            rech2["volante"] += 1; continue
        if ms < C_MIN_MS:
            rech2["lento"] += 1; continue
        ds = math.sqrt((r[X] - a[X]) ** 2 + (r[Z] - a[Z]) ** 2)
        if a[PITCH] != FALTA:
            pitch = math.radians(a[PITCH])          # el del framework: el mismo que usa el runtime
        else:
            pitch = 0.0
        if a[PITCH] == FALTA and ds > 0.01:
            pitch = math.asin(max(-1.0, min(1.0, (r[Y] - a[Y]) / math.sqrt(ds * ds + (r[Y] - a[Y]) ** 2))))
        if abs(pitch) > C_MAX_PITCH:
            rech2["pitch"] += 1; continue
        decel = (a[KMH] / 3.6 - ms) / dt
        sentido = -1.0 if a[GEAR] == 0 else 1.0
        plano = decel - sentido * 9.81 * math.sin(pitch)
        if plano < C_MIN_DECEL or plano > C_MAX_DECEL:
            rech2["rango"] += 1; continue
        b = min(NBINS - 1, max(0, int(r[KMH] / BINW)))
        s, n = bins.get(b, (0.0, 0))
        bins[b] = (s + plano, n + 1)
    print("  rechazos: gas %d . freno %d . volante %d . lento %d . pitch %d . fuera de rango %d"
          % (rech2["gas"], rech2["freno"], rech2["volante"], rech2["lento"],
             rech2["pitch"], rech2["rango"]))
    print("  bin   km/h      n     planeo medio")
    for b in sorted(bins):
        s, n = bins[b]
        marca = "   <-- EL BIN QUE EL RUNTIME NO PUEDE MEDIR" if b == 0 else ""
        print("   %2d  %3d-%3d  %5d     %.4f m/s2%s" % (b, b * 5, (b + 1) * 5, n, s / n, marca))
    print("  -> %d de %d bins poblados%s" % (len(bins), NBINS,
          "" if 0 in bins else "   ! el bin 0 tampoco"))

    # ---------------- steerMax ----------------
    print("")
    print("=== columnas de calibracion en la toma ===")
    for c in ("pitch_deg", "surface", "chassis_kmh"):
        est = "PRESENTE" if PRESENTES.get(c) else "ausente -> se deriva/omite (toma vieja)"
        print("  %-12s %s" % (c, est))
    if not PRESENTES.get("chassis_kmh"):
        print("  ! sin chassis_kmh se usa speed_kmh, que es el velocimetro de RUEDA. Difiere")
        print("    del chasis cuando el embrague desliza -- justo el regimen del endpoint.")
    if not PRESENTES.get("pitch_deg"):
        print("  ! el pitch se deriva de x,y,z: es la compuerta MAS sensible del banco de planeo")
        print("    (rechaza arriba de 2.2 grados) y derivarlo no es lo mismo que medirlo.")

    print("")
    print("=== steerMax ===")
    print("  max |front_wheel_deg| en la toma: %.2f deg" % max(abs(r[FWD]) for r in fil))
    return baldes, bins


def veredicto(baldes, bins, fil):
    print("")
    print("=== VEREDICTO PARA ESTA TOMA ===")
    giro_max = max(abs(r[FWD]) for r in fil)
    if len(baldes) < 4:
        print("  ✖ steerTransfer: %d/%d baldes. La toma no ejercita el rango de comando."
              % (len(baldes), ST_BUCKETS))
        print("    max |angulo de rueda| en la toma: %.2f deg." % giro_max)
        print("    Una toma solo calibra lo que RECORRIO: si el humano casi no doblo, no hay")
        print("    pares comando-angulo que extraer. No es del metodo, es de la toma.")
    else:
        print("  ok steerTransfer: %d/%d baldes poblados con el criterio del banco." % (len(baldes), ST_BUCKETS))
    if 0 in bins:
        print("  ok coastDecel bin 0 (0-5 km/h) POBLADO: %d muestras -> %.4f m/s2"
              % (bins[0][1], bins[0][0] / bins[0][1]))
        print("    Ese es el bin que el runtime NO puede llenar (el modulo actua todos los ticks).")
    else:
        print("  ✖ coastDecel bin 0 VACIO tambien en la toma.")
        print("    Esperable: entrando a una parada el humano FRENA, y el filtro exige freno~0.")
        print("    El bin 0 pide rodar suelto y lento, que no es lo que uno hace naturalmente.")
    print("  bins de planeo poblados: %d/%d" % (len(bins), NBINS))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("uso: envelope_desde_toma.py <frame_*.csv> [mas tomas...]")
        sys.exit(1)
    for ruta in sys.argv[1:]:
        b, c = analizar(ruta)
        veredicto(b, c, leer(ruta))
        print("")
        print("-" * 78)
