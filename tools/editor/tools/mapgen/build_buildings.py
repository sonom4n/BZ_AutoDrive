# EDIFICIOS para el editor: huellas orientadas + categoria, desde el scan de objetos.
#   python build_buildings.py [mapa]     -> buildings[.sfx].js  (window.__BUILDINGS)
#
# El scan (BZMapExtract) da por objeto: type,scenery,building,x,y,z,yaw,bw,bl,bh,debug.
# Tomamos building=1 (obj.IsBuilding()), filtramos clutter que no es edificio (lechos de arroyo,
# lamparas, letrinas, muros), y clasificamos por familia para darle color. Guardamos la CAJA de
# colision (bw,bl) + yaw => huella real orientada, no un punto.
import csv, json, math, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/
from _common import find_root, find_scan, sfx_of   # noqa: E402

W = sys.argv[1] if len(sys.argv) > 1 else "chernarusplus"
sfx = sfx_of(W)
ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))
SCAN = find_scan(W)

# categorias -> indice (el color vive en el editor, BUILD_COL, mismo orden).
# ORDEN de dibujo: se dibuja por indice, grandes al fondo (ver el sort de abajo).
CAT = {"pesada": 0, "industrial": 1, "agricola": 2, "caza": 3,
       "casa": 4, "casa2p": 5, "ladrillo": 6, "campo": 7,   # casa subdividida (madera 1p/2p, ladrillo, campo)
       "tenement": 8, "comercial": 9, "publico": 10, "religioso": 11, "militar": 12,
       "transporte": 13, "otro": 14}
# clutter que NO va al mapa de edificios (aunque el motor lo marque building=1):
# vehiculos (posicion aleatoria: wreck/boat/train/cart/bumper), carteles, andamios, marcadores.
SKIP = re.compile(r"streambed|lamp|light|lantern|toilet|doghouse|wall|fence|_gate|barrier|"
                  r"bench|bin|\bsign\b|pipe|wreck|ruin|stair|fireplace|grave|well_|"
                  r"bilboard|billboard|contaminated|scaffolding|through_static|lunapark|bumper|"
                  r"boat|train_7|train_wagon|cableway_cart", re.I)
# PRIMERO MATCH GANA -> lo especifico arriba de lo general.
FAM = [
    ("religioso",  r"church|chapel|cathedral|monaster"),
    ("caza",       r"deerstand|feedshack|hunting|feed_shack|treestand"),
    ("pesada",     r"cementworks|cement|sawmill|refinery|power_plant|smeltery|quarry"),
    ("transporte", r"busstop|bus_stop|cableway|_station|trainstation|depot_|airport|terminal|dock"),
    ("comercial",  r"shop|store|market|kiosk|city_stand|grocery|news\d|fastfood|restaurant|"
                   r"pub\b|hotel|bank|stand_|supermarket|mall"),
    ("publico",    r"school|hospital|medical|office|police|firestation|fire_|cinema|library|"
                   r"townhall|courthouse|castle|repair_center"),
    ("militar",    r"mil_|military|barrack|bunker|guard|checkpoint|hangar|watchtow|tower_small|"
                   r"heli|radar"),
    ("industrial", r"factory|warehouse|workshop|industrial|container|crane|fuel|power|"
                   r"tank|garage|silo_metal|repair_shed|substation"),
    ("agricola",   r"barn|shed|chicken|greenhouse|polytunnel|silo|coop|henhouse|stable|watertower|"
                   r"granary|hayrack|farm|feedshack"),
    ("tenement",   r"tenement|apartment|block_|panelak|highrise"),
    # CASA subdividida (primero match gana): ladrillo -> campo -> 2 plantas -> madera 1 planta
    ("ladrillo",   r"house_\db|house_1b|house_2b"),
    ("campo",      r"camp_house|\bcabin\b|chalet|cottage"),
    ("casa2p",     r"house_2w"),
    ("casa",       r"house|home|dvor|vila|villa|residential"),
]

def famof(t):
    tl = t.lower()
    for nom, pat in FAM:
        if re.search(pat, tl):
            return nom
    return "otro"

src = os.path.join(SCAN, f"{W}_objects.csv")
out = []
skip = 0
with open(src, encoding="latin1") as f:
    rd = csv.reader(f); hdr = next(rd)
    ix = {n: i for i, n in enumerate(hdr)}
    for r in rd:
        if len(r) < len(hdr) or r[ix["building"]] != "1":
            continue
        t = r[ix["type"]]
        if not t or SKIP.search(t):
            skip += 1; continue
        try:
            x = round(float(r[ix["x"]]), 1); z = round(float(r[ix["z"]]), 1)
            yaw = round(float(r[ix["yaw"]]), 1)
            bw = round(float(r[ix["bw"]]), 1); bl = round(float(r[ix["bl"]]), 1)
        except ValueError:
            continue
        if bw < 1 or bl < 1 or bw * bl > 5000:   # descarta puntitos y cajas gigantes espurias
            continue
        out.append([x, z, yaw, bw, bl, CAT[famof(t)]])

# ordenar por categoria (dibujo) y por area desc dentro (grandes al fondo)
out.sort(key=lambda b: (b[5], -(b[3] * b[4])))
dst = os.path.join(ROOT, f"buildings{sfx}.js")
with open(dst, "w", encoding="utf-8", newline="") as f:
    f.write("window.__BUILDINGS=" + json.dumps(out, separators=(",", ":")) + ";")

import collections
tal = collections.Counter(b[5] for b in out)
inv = {v: k for k, v in CAT.items()}
print(f"{W}: {len(out):,} edificios  (descarté clutter: {skip:,})")
for ci in sorted(tal):
    print(f"  {tal[ci]:6,}  {inv[ci]}")
print(f"-> {dst}  ({os.path.getsize(dst)/1e6:.1f} MB)")
