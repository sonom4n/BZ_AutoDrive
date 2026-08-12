# VEGETACION (bosque/matorral) desde la satelital -> mascara forest[.mapa].npy
#   python build_forest.py [mapa]
#
# POR QUE DESDE LA SATELITAL: el motor NO devuelve la vegetacion del terreno.
# Probado (2026-07-16): se amplio BZMapExtract para capturar arboles y en TODO Chernarus dio 781,
# y son todos "misc_tree_pavement" = arbolitos de vereda. Los arboles viven en el WRP (166 modelos
# en dz\plants\tree\), no como entidades consultables. La satelital es la via barata.
#
# METODO: el dosel es MUCHO mas oscuro que el campo => umbral de Otsu sobre el brillo.
# OJO: NO usar textura (varianza local). Se probo y marcaba el BORDE del bosque, no el bosque:
# el interior del dosel es oscuro pero PAREJO; la varianza esta en la transicion campo->arboles.
import os
import sys
import numpy as np
import cv2
from PIL import Image
Image.MAX_IMAGE_PIXELS = None

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/
from _common import find_root, find_scan, world_size, sfx_of   # noqa: E402

W = sys.argv[1] if len(sys.argv) > 1 else "chernarusplus"
ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))
_SCAN = find_scan(W)
sfx = sfx_of(W)
MAPW = world_size(_SCAN, W)                # del <world>_meta.csv, no clavado
TDIR = "sat_tiles" if not sfx else "sat_tiles/" + W
GRID = 32; IMG = 512; OV = 32; PITCH = IMG - OV; BORDER = OV // 2
SCALE = MAPW / (GRID * PITCH)            # m/px MEDIDO por mapa (Livonia 0.833, no 1.0)
PNG = os.path.join(ROOT, *TDIR.split("/"))
if not os.path.isdir(PNG):
    sys.exit(f"faltan los tiles: {PNG}")

CAN = GRID * PITCH
canvas = np.zeros((CAN, CAN, 3), np.uint8)
n = 0
for c in range(GRID):
    for r in range(GRID):
        p = os.path.join(PNG, f"t_{c}_{r}.jpg")
        if not os.path.exists(p):
            continue
        t = np.asarray(Image.open(p).convert("RGB"))
        x0 = PITCH*c - BORDER; y0 = PITCH*r - BORDER
        sx = max(0, -x0); sy = max(0, -y0)
        X0 = max(0, x0); Y0 = max(0, y0); X1 = min(CAN, x0+IMG); Y1 = min(CAN, y0+IMG)
        if X1 <= X0 or Y1 <= Y0:
            continue
        canvas[Y0:Y1, X0:X1] = t[sy:sy+(Y1-Y0), sx:sx+(X1-X0)]
        n += 1
px = int(MAPW / SCALE)
canvas = canvas[:px, :px]
print(f"{W}: {n} tiles · {canvas.shape} · {SCALE:.4f} m/px")

gray = cv2.cvtColor(canvas, cv2.COLOR_RGB2GRAY)
R, B = canvas[:, :, 0].astype(int), canvas[:, :, 2].astype(int)
land = ~((B > R + 10) & (B > 40))                 # sacar agua: el mar es azul y sesgaria el umbral
thr, _ = cv2.threshold(gray[land], 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
print(f"  umbral Otsu (solo tierra): {thr:.0f}")
veg = ((gray < thr) & land).astype(np.uint8) * 255
veg = cv2.morphologyEx(veg, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9)), 1)
veg = cv2.morphologyEx(veg, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (13, 13)), 1)

# --- LIMPIAR con el SCAN DE ALTURA: el umbral de brillo tambien marca el mar profundo (agua
#     oscura leida como dosel). El heightmap es medicion INDEPENDIENTE y sabe donde hay tierra
#     => dos fuentes corrigiendose. Medido: sin esto sobraba ~19% del mapa (falso bosque en el mar).
hp = os.path.join(_SCAN, f"{W}_height.csv")
if os.path.exists(hp):
    with open(hp) as f:
        hd = f.readline().strip().split(",")
        rows = [ln.strip() for ln in f if ln.strip()]
    hstep = float(dict(zip(hd[0::2], hd[1::2]))["step"])
    g = np.array([[float(v) for v in ln.split(",")] for ln in rows], dtype=np.float32)
    Hs = np.flipud(g.T)                                   # a imagen, norte arriba (como el sat)
    Hs = cv2.resize(Hs, (veg.shape[1], veg.shape[0]), interpolation=cv2.INTER_LINEAR)
    antes = (veg > 0).mean()
    veg[Hs <= 0.2] = 0
    print(f"  limpiado con altura: {100*antes:.1f}% -> {100*(veg>0).mean():.1f}% (saque el falso bosque del mar)")
else:
    print(f"  OJO: falta {W}_height.csv — la mascara va a incluir mar profundo como bosque")

np.save(os.path.join(ROOT, f"forest{sfx}.npy"), veg)

# --- PNG con ALPHA para la CAPA del editor: verde donde hay vegetacion, transparente donde no.
#     2.5 m/px = misma grilla que relief.png (asi calzan exacto). La mascara nace a 1 m/px (el sat),
#     asi que aca se BAJA resolucion: el borde del dosel sigue siendo real, no interpolado.
side = int(round(MAPW / 2.5)) + 1
vs = cv2.resize(veg, (side, side), interpolation=cv2.INTER_AREA)
rgba = np.zeros((side, side, 4), np.uint8)
rgba[:, :, 0] = 58; rgba[:, :, 1] = 122; rgba[:, :, 2] = 52   # verde bosque (BGR para cv2 -> ver abajo)
rgba[:, :, 3] = (vs.astype(np.float32) / 255 * 200).astype(np.uint8)   # alpha proporcional al dosel
png = os.path.join(ROOT, f"forest{sfx}.png")
cv2.imwrite(png, rgba[:, :, [2, 1, 0, 3]])                 # cv2 escribe BGRA
km2 = (veg > 0).sum() * SCALE * SCALE / 1e6
print(f"-> forest{sfx}.npy + forest{sfx}.png ({os.path.getsize(png)/1e6:.1f} MB, {side}x{side})")
print(f"   vegetacion {100*(veg>0).mean():.1f}% del mapa = {km2:.0f} km2")
print("   el editor la dibuja como CAPA APARTE sobre el relieve")
