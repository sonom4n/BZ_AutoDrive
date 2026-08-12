# TILES del fondo propio: RELIEVE + COBERTURA, a 1 m/px, para zoom sin techo.
#   python build_map_tiles.py [mapa]     mapa: chernarusplus | enoch | sakhal
#
# POR QUE TILES: un PNG unico del mapa entero a 1 m/px seria 15360x15360 = ~940 MB en RAM del
# navegador (Chrome no lo aguanta). Con tiles el editor carga SOLO lo visible, igual que el
# satelital. Y se procesa tile por tile => tampoco explota la RAM al construirlo.
#
# DOS CAPAS SEPARADAS a proposito (fusionadas, el verde tapaba el hipsometrico y no se leia la
# altura donde hay bosque):
#   relief_tiles<sfx>/t_<c>_<r>.jpg  - altura por color (hipsometrico) + hillshade. JPEG: es foto.
#   cover_tiles<sfx>/t_<c>_<r>.png   - cobertura clasificada, con ALPHA para que el relieve pase.
#
# PROCEDENCIA (importante):
#   RELIEVE   = MEDICION propia (SurfaceY in-game, BZMapExtract fase 3). Publicable.
#   COBERTURA = INFERIDA del satelital (k-means). El motor NO devuelve la vegetacion del terreno
#               (probado: 781 objetos = arbolitos de vereda). Es interpretacion, NO medicion:
#               sirve como fondo visual; el framework NO debe decidir nada con esto.
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
SCAN = find_scan(W)
sfx = sfx_of(W)
MAPW = world_size(SCAN, W)                 # del <world>_meta.csv: nada clavado por mapa
TDIR = "sat_tiles" if not sfx else os.path.join("sat_tiles", W)
print(f"raiz  : {ROOT}\nscan  : {SCAN}\nmapa  : {W}  {MAPW:.0f} m")
SGRID = 32; SIMG = 512; SOV = 32; SPITCH = SIMG - SOV; SBORDER = SOV // 2
SSCALE = MAPW / (SGRID * SPITCH)          # m/px del satelital, MEDIDO por mapa (Livonia 0.833)

TILE = 512                                 # px por tile de salida
MPP = 1.0                                  # 1 m/px
TM = TILE * MPP                            # metros que cubre un tile
NT = int(np.ceil(MAPW / TM))               # tiles por lado

# ---------------------------------------------------------------- satelital completo (para cobertura)
print(f"{W}: uniendo satelital ({SSCALE:.4f} m/px)...")
CAN = SGRID * SPITCH
sat = np.zeros((CAN, CAN, 3), np.uint8)
for c in range(SGRID):
    for r in range(SGRID):
        p = os.path.join(ROOT, TDIR, f"t_{c}_{r}.jpg")
        if not os.path.exists(p):
            continue
        t = np.asarray(Image.open(p).convert("RGB"))
        x0 = SPITCH*c - SBORDER; y0 = SPITCH*r - SBORDER
        sx = max(0, -x0); sy = max(0, -y0)
        X0 = max(0, x0); Y0 = max(0, y0); X1 = min(CAN, x0+SIMG); Y1 = min(CAN, y0+SIMG)
        if X1 <= X0 or Y1 <= Y0:
            continue
        sat[Y0:Y1, X0:X1] = t[sy:sy+(Y1-Y0), sx:sx+(X1-X0)]
sat = sat[:int(MAPW/SSCALE), :int(MAPW/SSCALE)]
if abs(SSCALE - MPP) > 1e-6:               # llevar a 1 m/px (Livonia viene a 0.833)
    n = int(round(MAPW / MPP))
    sat = cv2.resize(sat, (n, n), interpolation=cv2.INTER_AREA)
print(f"  sat a 1 m/px: {sat.shape}")

# ---------------------------------------------------------------- altura -> relieve
with open(os.path.join(SCAN, f"{W}_height.csv")) as f:
    hd = f.readline().strip().split(",")
    rows = [ln.strip() for ln in f if ln.strip()]
meta = dict(zip(hd[0::2], hd[1::2]))
HSTEP = float(meta["step"])
g = np.array([[float(v) for v in ln.split(",")] for ln in rows], dtype=np.float32)
Hbase = np.flipud(g.T)
side = int(round(MAPW / MPP))
H = cv2.resize(Hbase, (side, side), interpolation=cv2.INTER_CUBIC)   # a 1 m/px (bicubica: la
print(f"  altura {Hbase.shape} @ {HSTEP}m -> {H.shape} @ {MPP}m")    # superficie del motor tambien
                                                                      # es interpolada => es fiel
sea = H <= 0.2
land = ~sea

# hillshade multi-direccional con realce vertical (550m en 15km no se ve sin exagerar)
Z_EX = 3.0
gy, gx = np.gradient(H * Z_EX, MPP)
slope = np.arctan(np.hypot(gx, gy)); aspect = np.arctan2(-gx, gy)
hs = np.zeros_like(H)
for az_deg, wgt in ((315, 0.45), (45, 0.25), (225, 0.20), (135, 0.10)):
    az, alt = np.radians(az_deg), np.radians(45.0)
    hs += wgt * np.clip(np.sin(alt)*np.cos(slope) + np.cos(alt)*np.sin(slope)*np.cos(az - aspect), 0, 1)
hs = np.clip(hs, 0, 1)
del gy, gx, slope, aspect

# hipsometrico por PERCENTIL (cada banda ~= misma superficie => diferenciacion maxima)
RAMP = np.array([[42,78,52],[74,112,58],[116,144,68],[162,172,82],[206,194,104],
                 [214,166,92],[198,130,80],[166,100,74],[132,80,68]], np.float32) / 255
rel = np.zeros((side, side, 3), np.float32)
stops = np.maximum.accumulate(np.percentile(H[land], np.linspace(0, 100, len(RAMP))))
for c in range(3):
    rel[:, :, c] = np.interp(H, stops, RAMP[:, c])
if sea.any():                               # batimetria: el scan trae el fondo (hasta -231m)
    d = np.clip(H[sea], -250, 0)
    tt = (d - d.min()) / max(1e-6, (0 - d.min()))
    SEA = np.array([[10,26,48],[22,62,96],[58,110,140]], np.float32) / 255
    for c in range(3):
        rel[:, :, c][sea] = np.interp(tt, np.linspace(0, 1, len(SEA)), SEA[:, c])
rel = np.clip(rel * (0.30 + 1.00*hs)[:, :, None], 0, 1)
rel = (rel * 255).astype(np.uint8)
del hs

# ---------------------------------------------------------------- COBERTURA (k-means sobre el sat)
print("  clasificando cobertura...")
gray = cv2.cvtColor(sat, cv2.COLOR_RGB2GRAY)
k = 9
mean = cv2.blur(gray.astype(np.float32), (k, k))
sq = cv2.blur((gray.astype(np.float32))**2, (k, k))
tex = np.sqrt(np.maximum(sq - mean*mean, 0))            # textura: dosel alto, campo bajo
del mean, sq

feat_full = np.dstack([sat.astype(np.float32), tex]).reshape(-1, 4)
rng = np.random.default_rng(0)
idx = rng.choice(np.flatnonzero(land.ravel()), min(200_000, land.sum()), replace=False)
K = 5
_, _, cen = cv2.kmeans(feat_full[idx], K, None,
                       (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 25, 1.0), 4,
                       cv2.KMEANS_PP_CENTERS)
# asignar cada pixel a su centroide (por bloques: 236M pixeles no entran de una)
lab = np.empty(feat_full.shape[0], np.uint8)
B = 8_000_000
for i in range(0, feat_full.shape[0], B):
    d2 = ((feat_full[i:i+B, None, :] - cen[None, :, :])**2).sum(2)
    lab[i:i+B] = d2.argmin(1).astype(np.uint8)
lab = lab.reshape(side, side)
del feat_full

# ordenar clases por BRILLO: el dosel es lo mas oscuro, el suelo desnudo lo mas claro.
# Los colores son de la familia real del mapa (medida del sat: oliva desaturado, R~=G, B bajo).
order = np.argsort(cen[:, :3].mean(1))
COVER = np.array([[38, 66, 38],     # 0 mas oscuro  -> bosque cerrado
                  [64, 92, 48],     # 1             -> bosque ralo / matorral
                  [104, 122, 62],   # 2             -> pastizal
                  [146, 150, 88],   # 3             -> campo / pradera
                  [178, 168, 118]], np.uint8)  # 4 mas claro -> suelo desnudo / arado
remap = np.zeros(K, np.uint8)
for rank, ci in enumerate(order):
    remap[ci] = rank
cls = remap[lab]
# La clase se limpia poco (mediana 5) para no borrar carreteras (7m); el DETALLE lo aporta la
# textura, no la clase. Sonom4n notó que sobre-suavizando (mediana 9) se perdian las carreteras.
cls = cv2.medianBlur(cls, 5)
for rank in range(K):
    n = (cls[land] == rank).sum()
    print(f"    clase {rank}: {100*n/land.sum():5.1f}%  color {COVER[rank]}")
# COBERTURA MARCADA: color de clase (paleta del mapa) MODULADO por el brillo local, MAXIMIZANDO
# el detalle (Sonom4n: "lo mas detallado posible"). El color viene de la clase; el detalle fino
# (carreteras, copas, límites de campo) del brillo. Es RECOLOR, no la imagen cruda.
#   1) CLAHE: contraste LOCAL -> las carreteras resaltan igual en zona clara y oscura.
#   2) modulacion amplia (0.40..1.25): mas rango tonal = mas "marcado".
#   3) unsharp mask: bordes nitidos (la carretera se recorta del fondo).
clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
bc = clahe.apply(gray).astype(np.float32)
lo, hi = np.percentile(bc[land], 3), np.percentile(bc[land], 97)
bri = np.clip((bc - lo) / max(1e-6, hi - lo), 0, 1)
cov = (COVER[cls].astype(np.float32) * (0.40 + 0.85 * bri[:, :, None])).clip(0, 255).astype(np.uint8)
blur = cv2.GaussianBlur(cov, (0, 0), 1.2)
cov = cv2.addWeighted(cov, 1.6, blur, -0.6, 0)  # unsharp
cov[sea] = np.array([30, 55, 85], np.uint8)     # mar: azul plano (RGB); el relieve pone el detalle
del lab, tex, gray

# ---------------------------------------------------------------- escribir tiles
for name in (f"relief_tiles{sfx}", f"cover_tiles{sfx}"):
    os.makedirs(os.path.join(ROOT, name), exist_ok=True)
print(f"  escribiendo {NT}x{NT} = {NT*NT} tiles por capa ({TILE}px = {TM:.0f} m, {MPP} m/px)...")
for c in range(NT):
    for r in range(NT):
        x0 = c * TILE
        y0 = r * TILE                      # r=0 arriba = z alto (norte arriba), igual que el sat
        x1 = min(side, x0 + TILE); y1 = min(side, y0 + TILE)
        if x1 <= x0 or y1 <= y0:
            continue
        t = rel[y0:y1, x0:x1]
        if t.shape[0] != TILE or t.shape[1] != TILE:
            t = cv2.copyMakeBorder(t, 0, TILE-t.shape[0], 0, TILE-t.shape[1], cv2.BORDER_REPLICATE)
        cv2.imwrite(os.path.join(ROOT, f"relief_tiles{sfx}", f"t_{c}_{r}.jpg"),
                    t[:, :, ::-1], [cv2.IMWRITE_JPEG_QUALITY, 88])
        # COBERTURA como JPEG: ahora es tono continuo (textura), no 5 colores planos.
        cc = cov[y0:y1, x0:x1]
        if cc.shape[0] != TILE or cc.shape[1] != TILE:
            cc = cv2.copyMakeBorder(cc, 0, TILE-cc.shape[0], 0, TILE-cc.shape[1], cv2.BORDER_REPLICATE)
        cv2.imwrite(os.path.join(ROOT, f"cover_tiles{sfx}", f"t_{c}_{r}.jpg"),
                    cc[:, :, ::-1], [cv2.IMWRITE_JPEG_QUALITY, 92])
    print(f"    col {c+1}/{NT}", end="\r")

def mb(d):
    p = os.path.join(ROOT, d)
    return sum(os.path.getsize(os.path.join(p, f)) for f in os.listdir(p)) / 1e6
print(f"\n-> relief_tiles{sfx}/  {mb(f'relief_tiles{sfx}'):.0f} MB")
print(f"-> cover_tiles{sfx}/   {mb(f'cover_tiles{sfx}'):.0f} MB")
print(f"   grilla {NT}x{NT} · tile {TILE}px = {TM:.0f} m · {MPP} m/px")
