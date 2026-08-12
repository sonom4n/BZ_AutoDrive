# Huellas PINTADAS (material del terreno) -> centerlines RUTEABLES.
#   python build_huellas.py [mapa]        (por defecto: sakhal)
#
# SOLO SIRVE EN MAPAS TIPO SAKHAL, donde todo lo que no es camino es NIEVE => grava = camino.
# En mapas templados (Chernarus/Livonia) tierra y grava son TEXTURA DE TERRENO: se probó y dio
# 1.070 km de caminos inventados (90% falso). Ahí no se usa: sus caminos ya son objetos.
import csv, json, math, sys, os
import numpy as np, cv2
from skimage.morphology import skeletonize
import sknw

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/
from _common import find_root, find_scan, world_size, sfx_of   # noqa: E402

W = sys.argv[1] if len(sys.argv) > 1 else "sakhal"
ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))
SCAN = find_scan(W)
sfx = sfx_of(W)
MAPW = world_size(SCAN, W)
RES = 2.0
N = int(MAPW / RES)
WMAX = 12.0        # medio-ancho por-pixel (m): camino hasta ~24m pasa; mas ancho se recorta
# VOLCAN: se excluye por circulo (radial de ceniza, muy ancho). Centro/radio por mapa.
VOLC = {"sakhal": (10050, 11950, 900)}.get(W)

# --- celdas de material-camino (grava/tierra/asfalto/concreto/piedra; sin interiores ni escaleras) ---
mask = np.zeros((N, N), np.uint8)
keep = drop = 0
with open(os.path.join(SCAN, f"{W}_surfroad.csv"), encoding="latin1") as f:
    rd = csv.reader(f); next(rd)
    for r in rd:
        if len(r) < 3:
            continue
        t = r[2].lower()
        if "_int" in t or "stairs" in t:
            drop += 1; continue
        if not any(k in t for k in ("gravel", "dirt", "asphalt", "concrete", "stone")):
            drop += 1; continue
        try:
            x = float(r[0]); z = float(r[1])
        except ValueError:
            continue
        keep += 1
        ix = int(x / RES); iy = int((MAPW - z) / RES)
        if 0 <= ix < N and 0 <= iy < N:
            cv2.circle(mask, (ix, iy), 2, 255, -1)
print(f"{W}: celdas material-camino={keep:,} (descarte int/stairs/otros={drop:,})")
mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3)), 1)

# --- restar EDIFICIOS (obstacles) para matar cimientos/losas bajo construccion ---
bmask = np.zeros((N, N), np.uint8)
nb = 0
try:
    raw = open(os.path.join(ROOT, f"obstacles{sfx}.js"), encoding="utf-8", errors="replace").read()
    if '"' in raw:                                  # obstacles es un CSV embebido en un string JS
        ob = raw[raw.index('"') + 1:raw.rindex('"')].encode().decode("unicode_escape")
        rd = csv.reader(ob.splitlines()); next(rd)
        for c in rd:
            if len(c) < 6 or c[0] != "b":
                continue
            try:
                x = float(c[1]); z = float(c[2]); yaw = math.radians(float(c[3]))
                bw = float(c[4]); bl = float(c[5])
            except ValueError:
                continue
            sy, cy = math.sin(yaw), math.cos(yaw)
            hl = bl / 2 + 2; hw = bw / 2 + 2        # +2m de margen
            pts = np.array([[int((x + sy * a + cy * b) / RES), int((MAPW - (z + cy * a - sy * b)) / RES)]
                            for a, b in [(-hl, -hw), (hl, -hw), (hl, hw), (-hl, hw)]], np.int32)
            cv2.fillPoly(bmask, [pts], 255); nb += 1
except Exception as e:
    print(f"  (sin obstacles{sfx}.js: {e})")
mask = cv2.bitwise_and(mask, cv2.bitwise_not(bmask))
print(f"restados {nb} edificios (footprint)")

# --- excluir el VOLCAN (circulo) ---
if VOLC:
    vx, vz, vr = VOLC
    cv2.circle(mask, (int(vx / RES), int((MAPW - vz) / RES)), int(vr / RES), 0, -1)
    print(f"excluido volcan en ({vx},{vz}) r={vr}m")

# --- EXCLUIR EL AGUA con el HEIGHTMAP (2026-07-20) ---
# SurfaceGetType devuelve material TAMBIEN bajo el agua (el lecho marino es grava/arena) => se
# colaba como "camino". Sin heightmap no se podia detectar.
hp = os.path.join(SCAN, f"{W}_height.csv")
if os.path.exists(hp):
    with open(hp) as f:
        hd = f.readline().strip().split(",")
        rows = [ln.strip() for ln in f if ln.strip()]
    g = np.array([[float(v) for v in ln.split(",")] for ln in rows], dtype=np.float32)
    H = cv2.resize(np.flipud(g.T), (N, N), interpolation=cv2.INTER_LINEAR)   # norte arriba, como la mask
    antes = int((mask > 0).sum())
    mask[H <= 0.5] = 0                              # 0.5 m de margen sobre el nivel del mar
    print(f"agua excluida (heightmap): celdas {antes:,} -> {int((mask>0).sum()):,}")
else:
    print(f"OJO: falta {W}_height.csv - las huellas van a incluir el LECHO MARINO como camino")

# --- ANCHO POR-PIXEL: esqueleto de features angostas (camino hasta ~24m). Sin filtro por-componente
#     (tiraba caminos finos PEGADOS a una zona ancha, ej: causeway -> pueblo). ---
dt = cv2.distanceTransform(mask, cv2.DIST_L2, 5) * RES
sk = skeletonize(mask > 0) & (dt < WMAX)
print(f"esqueleto pix (dt<{WMAX}m): {int(sk.sum()):,}")

G = sknw.build_sknw(sk.astype(np.uint8), multi=True)


def elen(p):
    return float(np.sum(np.linalg.norm(np.diff(p, axis=0), axis=1))) * RES


for _ in range(6):                                  # podar espolones cortos
    rem = [(u, v, k) for u, v, k in G.edges(keys=True)
           if (G.degree(u) == 1) != (G.degree(v) == 1) and elen(G[u][v][k]["pts"]) < 6.0]
    if not rem:
        break
    for u, v, k in rem:
        try:
            G.remove_edge(u, v, k)
        except Exception:
            pass
    G.remove_nodes_from([n for n in list(G.nodes) if G.degree(n) == 0])


def rdp(points, eps):
    pts = np.array(points, float)
    if len(pts) < 3:
        return [list(pts[0]), list(pts[-1])]
    line = pts[-1] - pts[0]
    L = np.linalg.norm(line) + 1e-9
    d = np.abs(np.cross(line, pts - pts[0])) / L
    i = int(np.argmax(d))
    if d[i] > eps:
        return rdp(pts[:i + 1], eps)[:-1] + rdp(pts[i:], eps)
    return [list(pts[0]), list(pts[-1])]


polys = []
for u, v, k in G.edges(keys=True):
    ps = G[u][v][k]["pts"]
    if len(ps) < 2:
        continue
    # BUG 2026-07-20: la mask se llena con iy=(MAPW-z)/RES (norte arriba) pero aca se escribia
    # z=row*RES SIN deshacer esa inversion => TODAS las huellas salian ESPEJADAS en Z. Medido:
    # solo 42% caia en tierra (mediana -8,7 m = bajo el mar); desespejado da 99,9% y +11,2 m.
    # Lo detecto Sonom4n a ojo ("incluso esta invertida").  z = MAPW - row*RES
    world = rdp([[p[1] * RES, MAPW - p[0] * RES] for p in ps], 1.5)
    if len(world) >= 2 and elen(np.array(world)) >= 6:
        polys.append({"p": [[round(x, 1), round(z, 1)] for x, z in world]})

km = sum(sum(math.dist(a, b) for a, b in zip(pl["p"][:-1], pl["p"][1:])) for pl in polys) / 1000
dst = os.path.join(ROOT, f"huellas{sfx}.js")
with open(dst, "w", encoding="utf-8", newline="") as f:
    f.write("window.__HUELLAS=" + json.dumps(polys, separators=(",", ":")) + ";")
print(f"HUELLAS: {len(polys):,} polilineas - {km:.0f} km")
print(f"-> {dst}")
