# Relieve (hillshade) desde el scan de altura -> PNG que el editor carga como capa de fondo.
#   python build_relief.py [mapa]      mapa: chernarusplus | enoch | sakhal
#
# POR QUE UN PNG Y NO CALCULARLO EN EL NAVEGADOR: el sombreado es un gradiente sobre 2,4M de
# puntos; hacerlo en JS en cada redibujo es tirar el frame. Se hornea una vez, offline.
#
# FONDO PROPIO: esto es 100% MEDICION NUESTRA (SurfaceY del motor) => publicable, a diferencia
# del raster satelital, que son archivos extraidos del juego y NO se pueden redistribuir.
import os
import sys
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/
from _common import find_root, find_scan, sfx_of   # noqa: E402

W = sys.argv[1] if len(sys.argv) > 1 else "chernarusplus"
sfx = sfx_of(W)
ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))
SCAN = find_scan(W)

src = os.path.join(SCAN, f"{W}_height.csv")
if not os.path.exists(src):
    sys.exit(f"falta {src}\nCorrelo con RoadScanOnBoot=true + BZMapExtract.Begin() (fase 3).")

with open(src) as f:
    hdr = f.readline().strip().split(",")
    rows = [ln.strip() for ln in f if ln.strip()]
meta = dict(zip(hdr[0::2], hdr[1::2]))
STEP = float(meta["step"]); SIZE = float(meta["size"])
g = np.array([[float(v) for v in ln.split(",")] for ln in rows], dtype=np.float32)
print(f"{W}: grilla {g.shape} @ {STEP}m · mapa {SIZE:.0f}m · {g.min():.1f}..{g.max():.1f}m")

H = np.flipud(g.T)                       # a imagen: col=x, row=norte arriba

# --- RESOLUCION DE SALIDA: el dato es a STEP m, pero renderizar el PNG a STEP m/px se ve blando
# al hacer zoom. Se re-muestrea a OUT m/px con bicubica ANTES del sombreado.
# Es FIEL, no inventado: la superficie del motor tambien es interpolada entre puntos de grilla
# (SurfaceY devuelve esa superficie), asi que el hillshade fino reproduce el terreno real.
OUT = 2.5
N = int(round(SIZE / OUT)) + 1
if abs(OUT - STEP) > 1e-6:
    H = cv2.resize(H, (N, N), interpolation=cv2.INTER_CUBIC)
    print(f"  render a {OUT} m/px -> {N}x{N} (dato {STEP} m/px, bicubica)")
STEP = OUT                               # a partir de aca todo trabaja en la grilla de salida

# --- SOMBREADO: multi-direccional. Un solo sol (315) deja las laderas que miran al SE planchadas;
#     promediar 4 azimuts da volumen en TODAS las orientaciones sin sombras duras. El realce vertical
#     (Z_EX) exagera el desnivel: Chernarus tiene 550 m en 15 km, sin exagerar no se ve nada.
Z_EX = 3.0
gy, gx = np.gradient(H * Z_EX, STEP)
slope = np.arctan(np.hypot(gx, gy))
aspect = np.arctan2(-gx, gy)
hs = np.zeros_like(H)
for az_deg, w in ((315, 0.45), (45, 0.25), (225, 0.20), (135, 0.10)):
    az, alt = np.radians(az_deg), np.radians(45.0)
    hs += w * np.clip(np.sin(alt)*np.cos(slope) + np.cos(alt)*np.sin(slope)*np.cos(az - aspect), 0, 1)
hs = np.clip(hs, 0, 1)

sea = H <= 0.2
land = ~sea
img = np.zeros((N, N, 3), np.float32)

# --- HIPSOMETRICO por PERCENTILES: es una capa de RELIEVE y se asume como tal (Sonom4n, 2026-07-16):
#     "estamos midiendo relieve (sin vegetacion), no podemos simularlo desde ahi, entonces para la
#     capa relieve esta bien que sea una paleta detallada y bien diferenciada en alturas".
#     => el color NO imita cobertura (no sabe de bosques): CODIFICA ALTURA lo mejor posible.
#     Cortes por PERCENTIL y no lineales: la mediana de Chernarus esta en 230 m y el 44% de la
#     tierra cae entre 170-420 m; una rampa lineal 0-550 metia casi todo en 2 colores = lavado.
#     Cada banda cubre ~la misma SUPERFICIE => diferenciacion maxima. Se adapta a cada mapa.
#     SIN BLANCO: 550 m no es nieve (Chernarus no tiene) y el blanco se leia como tal.
RAMP = np.array([
    [ 42,  78,  52],   # verde oscuro  - lo mas bajo
    [ 74, 112,  58],   # verde
    [116, 144,  68],   # verde claro
    [162, 172,  82],   # verde-amarillo
    [206, 194, 104],   # amarillo
    [214, 166,  92],   # ocre
    [198, 130,  80],   # naranja-marron
    [166, 100,  74],   # marron
    [132,  80,  68],   # marron oscuro - cimas
], np.float32) / 255
if land.any():
    qs = np.linspace(0, 100, len(RAMP))
    stops = np.percentile(H[land], qs)             # cortes = percentiles reales de ESTE mapa
    stops = np.maximum.accumulate(stops)           # monotono (mapas chatos pueden empatar)
    for c in range(3):
        img[:, :, c] = np.interp(H, stops, RAMP[:, c])

# --- MAR por PROFUNDIDAD: el scan trae el fondo (Chernarus llega a -231 m) => batimetria real,
#     no un azul plano. Da la plataforma costera y hace leer la costa.
if sea.any():
    d = np.clip(H[sea], -250, 0)
    tt = (d - d.min()) / max(1e-6, (0 - d.min()))  # 0 = hondo, 1 = orilla
    SEA = np.array([[10, 26, 48], [22, 62, 96], [58, 110, 140]], np.float32) / 255
    for c in range(3):
        img[:, :, c][sea] = np.interp(tt, np.linspace(0, 1, len(SEA)), SEA[:, c])

# La VEGETACION va en su PROPIA capa (build_forest.py -> forest[.mapa].png), NO horneada aca:
# fusionadas, el verde tapaba el hipsometrico en el 47% del mapa y no se leia la altura donde hay
# bosque. Son dos mediciones distintas (altura in-game / dosel del satelital) => dos capas.
img = np.clip(img * (0.30 + 1.00 * hs)[:, :, None], 0, 1)   # sombreado con mas pegada = mas 3D

dst = os.path.join(ROOT, f"relief{sfx}.png")
cv2.imwrite(dst, (img[:, :, ::-1] * 255).astype(np.uint8), [cv2.IMWRITE_PNG_COMPRESSION, 6])
print(f"-> {dst}  ({os.path.getsize(dst)/1e6:.1f} MB, {N}x{N} px = {STEP} m/px)")
print("   el editor lo estira sobre el mapa completo: 1 px = %.0f m" % STEP)
