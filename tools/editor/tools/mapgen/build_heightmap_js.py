# Heightmap para el EDITOR (capa de elevación del perfil de velocidad).
#   python build_heightmap_js.py [mapa]        -> heightmap[.sfx].js
#
# El editor lee window.__HEIGHTMAP = {minx,minz,cell,cols,rows,data} y muestrea la altura del
# terreno bajo la traza (bilineal). El dato viene del scan (<world>_height.csv, fase 3 de
# BZMapExtract). Se BAJA a 10 m (del 5 m nativo): sobra para el perfil y pesa la mitar.
#
# OJO con la orientación: el CSV tiene UNA fila por columna X (X=fila·step) con las Z dentro
# (Z=valor·step) => grid[xi][zi]. El editor quiere data[ri*cols+ci] con ci=x, ri=z => hay que
# TRANSPONER a [z][x]. (Mismo tipo de bug que espejó las huellas; acá se cuida.)
import json
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/
from _common import find_root, find_scan, sfx_of   # noqa: E402

W = sys.argv[1] if len(sys.argv) > 1 else "chernarusplus"
ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))
SCAN = find_scan(W)
sfx = sfx_of(W)
OUT_CELL = 10.0                                     # resolución de salida (m)

src = os.path.join(SCAN, f"{W}_height.csv")
if not os.path.exists(src):
    sys.exit(f"falta {src} (corré el scan: RoadScanOnBoot + BZMapExtract fase 3)")

with open(src) as f:
    hd = f.readline().strip().split(",")
    rows = [ln.strip() for ln in f if ln.strip()]
meta = dict(zip(hd[0::2], hd[1::2]))
STEP = float(meta["step"]); SIZE = float(meta["size"])
g = np.array([[float(v) for v in ln.split(",")] for ln in rows], dtype=np.float32)   # g[xi][zi]
print(f"{W}: grilla {g.shape} @ {STEP} m")

stride = max(1, int(round(OUT_CELL / STEP)))
cell = STEP * stride
ds = g[::stride, ::stride]                          # [x][z]
data = ds.T                                         # -> [z][x]  (lo que espera el editor)
rows_z, cols_x = data.shape
flat = [round(float(v), 1) for v in data.reshape(-1)]

obj = {"minx": 0, "minz": 0, "cell": cell, "cols": cols_x, "rows": rows_z, "data": flat}
dst = os.path.join(ROOT, f"heightmap{sfx}.js")
with open(dst, "w", encoding="utf-8", newline="") as f:
    f.write("window.__HEIGHTMAP=" + json.dumps(obj, separators=(",", ":")) + ";")

print(f"-> {dst}  ({os.path.getsize(dst)/1e6:.1f} MB)  cell {cell:.0f} m · {cols_x}x{rows_z} = {len(flat):,} alturas")
print(f"   rango {min(flat):.0f}..{max(flat):.0f} m")
