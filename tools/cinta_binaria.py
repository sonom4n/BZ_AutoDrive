# -*- coding: utf-8 -*-
"""Generates the binary of a route tape (<base>_cinta.bin) from its CSV, so the server reads it in a fraction of the time.

The CSV stays the source: this only adds files, and the binary can be deleted at any time. If it is missing, stale or
damaged, the server loads the CSV (and says so when one was there and did not check out).

Usage:
    python cinta_binaria.py <folder>                     every *_cinta.csv in the folder
    python cinta_binaria.py <folder> --solo NAME         only tapes whose name contains NAME
    python cinta_binaria.py <folder> --una FILE          only this tape (the exact name of its CSV)
    python cinta_binaria.py <folder> --salida <folder2>  writes the binaries somewhere else
    python cinta_binaria.py <folder> --forzar            rewrites binaries that are already up to date
    python cinta_binaria.py <folder> --estado            writes nothing: says the state of each tape (al dia, sin binario, desactualizado)
    python cinta_binaria.py <folder> --estado --completo the same, also checking the sum of each binary (the server's whole check)
    --maquina                                            one line per tape for the wizard: RESULT|what|rows|file|detail
"""
import argparse
import array
import glob
import math
import os
import re
import struct
import sys

MAGIA = b"BZCINTA"
VERSION = 3
FIN = 1313424962
TROZO_BYTES = 4096
HUELLA_PASO = 64
SUMA_DESDE = 9
COLUMNAS_FLOAT = ("s", "x", "z", "y", "h", "kmh", "fw", "thr", "brk", "str", "roll", "camina")
COLUMNAS_INT = ("gear", "corte")
NUM_FLOAT = re.compile(r"^-?\d+(\.\d+)?([eE][-+]?\d+)?$")
NUM_INT = re.compile(r"^-?\d+$")


class TomaRara(Exception):
    """A tape this tool will not vouch for: the server keeps reading its CSV."""


def huella(datos):
    """Size is the caller's. XOR of one int every HUELLA_PASO, per 4 KB piece, mixed with a running counter: the same as Huella in BZCintaBinario.c."""
    h = 0
    j = 0
    for ini in range(0, len(datos), TROZO_BYTES):
        ints = min(TROZO_BYTES, len(datos) - ini) // 4
        for k in range(0, ints, HUELLA_PASO):
            h ^= struct.unpack_from("<i", datos, ini + k * 4)[0] ^ j
            j += 1
    return h


def suma(datos):
    """h = h * 31 + int over everything after the 35-byte header, in signed 32 bits: the same as SumaDelBinario in BZCintaBinario.c."""
    a = array.array("i")
    a.frombytes(datos[:len(datos) // 4 * 4])
    if sys.byteorder != "little":
        a.byteswap()
    h = 0
    for v in a[SUMA_DESDE:]:
        h = (h * 31 + v) & 0xFFFFFFFF
    return h - 0x100000000 if h >= 0x80000000 else h


def columnas_de_csv(crudo):
    """The rules of AgregarFila: the first line is the header, a row with fewer than 12 columns does not count, the last 4 columns are optional."""
    lineas = crudo.decode("ascii").split("\n")
    if lineas and lineas[-1] == "":
        lineas.pop()
    f = {k: [] for k in COLUMNAS_FLOAT + COLUMNAS_INT}
    for n, l in enumerate(lineas[1:], 2):
        t = l.rstrip("\r").split(",")
        if len(t) < 12:
            continue
        for x in t:
            if not NUM_FLOAT.match(x):
                raise TomaRara("line %d: %r is not a plain number" % (n, x))
        if not NUM_INT.match(t[9]) or (len(t) > 13 and not NUM_INT.match(t[13])):
            raise TomaRara("line %d: gear or cut is not an integer" % n)
        f["s"].append(float(t[0]))
        f["x"].append(float(t[1]))
        f["y"].append(float(t[2]))
        f["z"].append(float(t[3]))
        f["h"].append(float(t[4]))
        f["kmh"].append(float(t[5]))
        f["fw"].append(float(t[6]))
        f["thr"].append(float(t[7]))
        f["brk"].append(float(t[8]))
        f["gear"].append(int(t[9]))
        f["str"].append(float(t[12]) if len(t) > 12 else -9.0)
        f["corte"].append(int(t[13]) if len(t) > 13 else 0)
        f["roll"].append(float(t[14]) if len(t) > 14 else -99.0)
        f["camina"].append(float(t[15]) if len(t) > 15 else 0.0)
    if not f["s"]:
        raise TomaRara("no rows")
    return f


def _a_bytes(codigo, valores):
    a = array.array(codigo, valores)
    assert a.itemsize == 4
    if sys.byteorder != "little":
        a.byteswap()
    return a.tobytes()


def armar(crudo):
    """The whole binary file, as bytes, from the bytes of the CSV."""
    if len(crudo) >= 2 ** 31:
        raise TomaRara("too big")
    f = columnas_de_csv(crudo)
    n = len(f["s"])
    cuerpo = []
    for k in COLUMNAS_FLOAT:
        a = _a_bytes("f", f[k])
        if not all(math.isfinite(x) for x in struct.unpack("<%df" % n, a)):
            raise TomaRara("column %s has a value out of range" % k)
        cuerpo.append(struct.pack("<i", n) + a)
    for k in COLUMNAS_INT:
        cuerpo.append(struct.pack("<i", n) + _a_bytes("i", f[k]))
    cuerpo.append(struct.pack("<i", FIN))
    inicio = struct.pack("<i", len(MAGIA)) + MAGIA + struct.pack("<iii", VERSION, n, len(crudo))
    resto = b"".join(cuerpo)
    tam = len(inicio) + 12 + len(resto)
    # the sum starts after the header, so the file does not depend on itself
    datos = inicio + struct.pack("<iii", huella(crudo), tam, 0) + resto
    h = suma(datos)
    final = inicio + struct.pack("<iii", huella(crudo), tam, h) + resto
    assert len(final) == tam
    return final, n


def estado(ruta_bin, crudo, completo=True):
    """The state of a tape, checking what the server checks: ("al dia" | "sin binario" | "desactualizado", rows, why).
    why is one word the wizard translates: format, version, csv or damaged.
    Without completo it does not read the whole binary: it does not see damage inside it, only a stale or cut file."""
    filas = max(crudo.count(b"\n") - (1 if crudo.endswith(b"\n") else 0), 0)
    if not os.path.exists(ruta_bin):
        return "sin binario", filas, ""
    with open(ruta_bin, "rb") as h:
        b = h.read() if completo else h.read(35)
    tam = len(b) if completo else os.path.getsize(ruta_bin)
    if len(b) < 35 or b[:4] != struct.pack("<i", len(MAGIA)) or b[4:11] != MAGIA:
        return "desactualizado", filas, "format"
    ver, n, tam_csv, h_csv, tam_bin, h_bin = struct.unpack_from("<iiiiii", b, 11)
    if ver != VERSION:
        return "desactualizado", filas, "version"
    if tam_csv != len(crudo) or h_csv != huella(crudo):
        return "desactualizado", filas, "csv"
    if tam_bin != tam:
        return "desactualizado", filas, "damaged"
    if completo and (h_bin != suma(b) or struct.unpack_from("<i", b, len(b) - 4)[0] != FIN):
        return "desactualizado", filas, "damaged"
    return "al dia", n, ""


def al_dia(ruta_bin, crudo):
    """True if the binary on disk is the one this CSV would give."""
    return estado(ruta_bin, crudo)[0] == "al dia"


def escribir(ruta_csv, ruta_bin=None, forzar=False):
    """Writes the binary of one tape. Returns (what happened, rows). Never touches the CSV."""
    if ruta_bin is None:
        ruta_bin = ruta_csv[:-3] + "bin"
    crudo = open(ruta_csv, "rb").read()
    if not forzar and al_dia(ruta_bin, crudo):
        return "al dia", 0
    try:
        datos, n = armar(crudo)
    except (TomaRara, UnicodeDecodeError, ValueError, OverflowError) as e:
        # an old binary next to a tape that cannot be converted: the server would discard it anyway, but it does not stay
        if os.path.exists(ruta_bin):
            os.remove(ruta_bin)
        return "rechazada: %s" % e, 0
    tmp = ruta_bin + ".tmp"
    try:
        with open(tmp, "wb") as h:
            h.write(datos)
        os.replace(tmp, ruta_bin)
    except OSError as e:
        if os.path.exists(tmp):
            os.remove(tmp)
        return "fallo: %s" % e, 0
    return "escrito", n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("carpeta")
    ap.add_argument("--solo", default="")
    ap.add_argument("--una", default="")
    ap.add_argument("--estado", action="store_true")
    ap.add_argument("--completo", action="store_true")
    ap.add_argument("--maquina", action="store_true")
    ap.add_argument("--salida", default="")
    ap.add_argument("--forzar", action="store_true")
    a = ap.parse_args()
    # a name the console cannot show must not stop the tool: the same as frame_to_route.py
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
        sys.stderr.reconfigure(errors="replace")
    cuenta = {}
    for p in sorted(glob.glob(os.path.join(a.carpeta, "*_cinta.csv"))):
        nom = os.path.basename(p)
        if a.solo and a.solo not in nom:
            continue
        if a.una and a.una != nom:
            continue
        destino = nom[:-3] + "bin"
        if a.salida:
            os.makedirs(a.salida, exist_ok=True)
            destino = os.path.join(a.salida, destino)
        else:
            destino = p[:-3] + "bin"
        # one tape that cannot be read or written does not stop the others
        detalle = ""
        try:
            if a.estado:
                with open(p, "rb") as h:
                    que, n, detalle = estado(destino, h.read(), a.completo)
            else:
                que, n = escribir(p, destino, a.forzar)
        except OSError as e:
            que, n = "fallo: %s" % e, 0
        clave = que.split(":")[0]
        cuenta[clave] = cuenta.get(clave, 0) + 1
        if a.maquina:
            if ":" in que:
                que, detalle = que.split(":", 1)
            print("RESULT|%s|%d|%s|%s" % (que, n, nom, detalle.strip().replace("|", "/").replace("\n", " ")))
        elif a.estado:
            print("%-50s %s (%d rows)%s" % (nom, que, n, " - " + detalle if detalle else ""))
        else:
            print("%-50s %s%s" % (nom, que, " (%d rows)" % n if n else ""))
    if not a.maquina:
        print("total:", cuenta)
    return 0


if __name__ == "__main__":
    sys.exit(main())
